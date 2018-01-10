#ifdef _SISCI
#include <sisci_types.h>
#include <sisci_error.h>
#include <sisci_api.h>
#ifndef __DIS_CLUSTER__
#define __DIS_CLUSTER__
#endif
#endif

#include <nvm_types.h>
#include <nvm_ctrl.h>
#include <nvm_util.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdio.h>
#include "dis_device.h"
#include "ctrl.h"
#include "util.h"
#include "regs.h"
#include "dprintf.h"


/*
 * Controller device type.
 * Indicates how we access the controller.
 */
enum device_type
{
    _DEVICE_TYPE_UNKNOWN        = 0x00, // Device is mapped manually by the user
    _DEVICE_TYPE_SYSFS          = 0x01, // Device is mapped through file descriptor
    _DEVICE_TYPE_SMARTIO        = 0x04  // Device is mapped by SISCI SmartIO API
};



/*
 * Internal handle container.
 */
struct controller
{
    enum device_type        type;       // Device type
    int                     fd;         // File descriptor to memory mapping
    struct device_memory*   bar;        // Reference to mapped BAR0
    nvm_ctrl_t              handle;     // User handle
};



/* Convenience defines */
#define encode_page_size(ps)    _nvm_b2log((ps) >> 12)
#define encode_entry_size(es)   _nvm_b2log(es)

#define container(ctrl)         \
    ((struct controller*) (((unsigned char*) (ctrl)) - offsetof(struct controller, handle)))

#define const_container(ctrl)   \
    ((const struct controller*) (((const unsigned char*) (ctrl)) - offsetof(struct controller, handle)))





/*
 * Look up file descriptor from controller handle.
 */
int _nvm_fd_from_ctrl(const nvm_ctrl_t* ctrl)
{
    const struct controller* container = const_container(ctrl);

    switch (container->type)
    {
        case _DEVICE_TYPE_SYSFS:
            return container->fd;

        default:
            return -EBADF;
    }
}



#ifdef _SISCI
/*
 * Look up device from controller handle.
 */
const struct device* _nvm_device_from_ctrl(const nvm_ctrl_t* ctrl)
{
    const struct controller* container = const_container(ctrl);

    if (container->type == _DEVICE_TYPE_SMARTIO && container->bar != NULL)
    {
        return &container->bar->device;
    }

    return NULL;
}
#endif



/*
 * Helper function to allocate a handle container.
 */
static struct controller* create_container()
{
    struct controller* container = (struct controller*) malloc(sizeof(struct controller));
    
    if (container == NULL)
    {
        dprintf("Failed to allocate controller handle: %s\n", strerror(errno));
        return NULL;
    }

    container->type = _DEVICE_TYPE_UNKNOWN;
    container->fd = -1;
    container->bar = NULL;

    return container;
}



#ifdef _SISCI
/*
 * Helper function to increase a device reference and connect 
 * to a PCI BAR0 on the controller's device.
 */
static int connect_register_memory(struct device_memory** handle, const struct device* dev, uint32_t adapter)
{
    *handle = NULL;

    struct device_memory* mem = (struct device_memory*) malloc(sizeof(struct device_memory));
    if (mem == NULL)
    {
        dprintf("Failed to allocate controller memory reference: %s\n", strerror(errno));
        return ENOMEM;
    }

    int err = _nvm_device_memory_get(mem, dev, adapter, 0, NVM_CTRL_MEM_MINSIZE, true, SCI_FLAG_BAR);
    if (err != 0)
    {
        free(mem);
        dprintf("Failed to get controller memory reference: %s\n", strerror(err));
        return err;
    }

    *handle = mem;
    return 0;
}
#endif



/* 
 * Helper function to initialize the controller handle by reading
 * the appropriate registers from the controller BAR.
 */
static int initialize_handle(nvm_ctrl_t* ctrl, volatile void* mm_ptr, size_t mm_size)
{
    if (mm_size < NVM_CTRL_MEM_MINSIZE)
    {
        return EINVAL;
    }

    ctrl->mm_size = mm_size;
    ctrl->mm_ptr = mm_ptr;

    // Get the system page size
    size_t page_size = _nvm_host_page_size();
    if (page_size == 0)
    {
        return ENOMEM;
    }

    // Get the controller page size
    uint8_t host_page_size = encode_page_size(page_size);
    uint8_t max_page_size = CAP$MPSMAX(mm_ptr);
    uint8_t min_page_size = CAP$MPSMIN(mm_ptr);

    if ( ! (min_page_size <= host_page_size && host_page_size <= max_page_size) )
    {
        dprintf("System page size is incompatible with controller page size\n");
        return ERANGE;
    }

    // Set controller properties
    ctrl->page_size = page_size;
    ctrl->dstrd = CAP$DSTRD(mm_ptr);
    ctrl->timeout = CAP$TO(mm_ptr) * 500UL;
    ctrl->max_entries = CAP$MQES(mm_ptr) + 1; // CAP.MQES is 0's based

    return 0;
}



int nvm_raw_ctrl_reset(const nvm_ctrl_t* ctrl, uint64_t acq_addr, uint64_t asq_addr)
{
    volatile uint32_t* cc = CC(ctrl->mm_ptr);

    // Set CC.EN to 0
    *cc = *cc & ~1;

    // Wait for CSTS.RDY to transition from 1 to 0
    uint64_t timeout = ctrl->timeout * 1000000UL;
    uint64_t remaining = _nvm_delay_remain(timeout);

    while (CSTS$RDY(ctrl->mm_ptr) != 0)
    {
        if (remaining == 0)
        {
            dprintf("Timeout exceeded while waiting for controller reset\n");
            return ETIME;
        }

        remaining = _nvm_delay_remain(remaining);
    }

    // Set admin queue attributes
    volatile uint32_t* aqa = AQA(ctrl->mm_ptr);
    uint32_t cq_max_entries = (ctrl->page_size / sizeof(nvm_cpl_t)) - 1;
    uint32_t sq_max_entries = (ctrl->page_size / sizeof(nvm_cmd_t)) - 1;
    *aqa = AQA$AQS(sq_max_entries) | AQA$AQC(cq_max_entries);
    
    // Set admin completion queue
    volatile uint64_t* acq = ACQ(ctrl->mm_ptr);
    *acq = acq_addr;

    // Set admin submission queue
    volatile uint64_t* asq = ASQ(ctrl->mm_ptr);
    *asq = asq_addr;

    // Set CC.MPS to pagesize and CC.EN to 1
    uint32_t cqes = encode_entry_size(sizeof(nvm_cpl_t)); 
    uint32_t sqes = encode_entry_size(sizeof(nvm_cmd_t)); 
    *cc = CC$IOCQES(cqes) | CC$IOSQES(sqes) | CC$MPS(encode_page_size(ctrl->page_size)) | CC$CSS(0) | CC$EN(1);

    // Wait for CSTS.RDY to transition from 0 to 1
    remaining = _nvm_delay_remain(timeout);

    while (CSTS$RDY(ctrl->mm_ptr) != 1)
    {
        if (remaining == 0)
        {
            dprintf("Timeout exceeded while waiting for controller enable\n");
            return ETIME;
        }

        remaining = _nvm_delay_remain(remaining);
    }

    return 0;
}



int nvm_raw_ctrl_init(nvm_ctrl_t** ctrl, volatile void* mm_ptr, size_t mm_size)
{
    int err;
    *ctrl = NULL;

    struct controller* container = create_container();
    if (container == NULL)
    {
        return ENOMEM;
    }

    container->type = _DEVICE_TYPE_UNKNOWN;

    err = initialize_handle(&container->handle, mm_ptr, mm_size);
    if (err != 0)
    {
        free(container);
        return err;
    }

    *ctrl = &container->handle;
    return 0;
}



#ifdef _SISCI
int nvm_dis_ctrl_init(nvm_ctrl_t** ctrl, uint64_t dev_id, uint32_t adapter)
{
    int err;
    *ctrl = NULL;

    struct controller* container = create_container();
    if (container == NULL)
    {
        return ENOMEM;
    }

    container->type = _DEVICE_TYPE_SMARTIO;

    struct device dev;
    err = _nvm_device_get(&dev, dev_id);
    if (err != 0)
    {
        free(container);
        return err;
    }

    err = connect_register_memory(&container->bar, &dev, adapter);
    if (err != 0)
    {
        _nvm_device_put(&dev);
        free(container);
        return err;
    }

    _nvm_device_put(&dev);

    size_t size = SCIGetRemoteSegmentSize(container->bar->segment);

    err = initialize_handle(&container->handle, container->bar->va_mapping.vaddr, size);
    if (err != 0)
    {
        _nvm_device_memory_put(container->bar);
        free(container);
        return err;
    }

    *ctrl = &container->handle;
    return 0;
}
#endif



int nvm_ctrl_init(nvm_ctrl_t** ctrl, const char* path)
{
    int err;
    
    int fd = open(path, O_RDWR|O_NONBLOCK);
    if (fd < 0)
    {
        dprintf("Could not find device resource file: %s\n", path);
        return ENODEV;
    }

    volatile void* ptr = mmap(NULL, NVM_CTRL_MEM_MINSIZE, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_FILE, fd, 0);
    if (ptr == NULL)
    {
        close(fd);
        dprintf("Failed to map device memory: %s\n", strerror(errno));
        return EIO;
    }

    struct controller* container = create_container();
    if (container == NULL)
    {
        munmap((void*) ptr, NVM_CTRL_MEM_MINSIZE);
        close(fd);
        return ENOMEM;
    }

    container->type = _DEVICE_TYPE_SYSFS;
    container->fd = fd;

    err = initialize_handle(&container->handle, ptr, NVM_CTRL_MEM_MINSIZE);
    if (err != 0)
    {
        munmap((void*) ptr, NVM_CTRL_MEM_MINSIZE);
        close(fd);
        free(container);
        return err;
    }

    *ctrl = &container->handle;
    return 0;
}



void nvm_ctrl_free(nvm_ctrl_t* ctrl)
{
    if (ctrl != NULL)
    {
        struct controller* container = container(ctrl);

        switch (container->type)
        {
            case _DEVICE_TYPE_UNKNOWN:
                // Do nothing
                break;

            case _DEVICE_TYPE_SYSFS:
                munmap((void*) ctrl->mm_ptr, ctrl->mm_size);
                close(container->fd);
                break;

#if _SISCI
            case _DEVICE_TYPE_SMARTIO:
                _nvm_device_memory_put(container->bar);
                free(container->bar);
                break;
#endif

            default:
                dprintf("Unknown controller type\n");
                break;
        }

        free(container);
    }
}



//#ifdef _SISCI
//int nvm_dis_ctrl_device(const nvm_ctrl_t* ctrl, sci_device_t* dev)
//{
//    *dev = NULL;
//
//    const struct device* device = _nvm_device_from_ctrl(ctrl);
//
//    if (device != NULL)
//    {
//        *dev = device->device;
//        return 0;
//    }
//
//    return EBADF;
//}
//#endif

