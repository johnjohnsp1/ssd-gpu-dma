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
#include <nvm_dma.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include "ioctl.h"
#include "dev.h"
//#include "ctrl.h"
#include "util.h"
#include "regs.h"
#include "dprintf.h"


/* Forward declarations */
struct segment;



/* 
 * Mapping type.
 */
enum map_type
{
    _MAP_TYPE_UNKNOWN               = 0x00,
    _MAP_TYPE_SMARTIO               = 0x01,
    _MAP_TYPE_IOCTL_HOST            = 0x02,
    _MAP_TYPE_IOCTL_DEVICE          = 0x04
};



/*
 * Internal map container.
 */
struct map
{
    enum map_type           type;       // Mapping type
    int                     ioctl_fd;   // File descriptor to kernel module
    struct segment*         segment;    // Segment container
    nvm_dma_t               handle;     // The DMA mapping descriptor
} __attribute__((aligned (32)));



#ifdef _SISCI
/*
 * Segment type.
 */
enum segment_type
{
    _SEGMENT_TYPE_UNKNOWN           = 0x00,
    _SEGMENT_TYPE_REMOTE            = 0x01,
    _SEGMENT_TYPE_LOCAL             = 0x02
};
#endif /* _SISCI */



#ifdef _SISCI
/*
 * Reverse mapped segment descriptor.
 *
 * This structure holds a SmartIO to the controller device,
 * and a reference to the segment mapped for the controller.
 */
struct segment
{
    struct device*          device;
    enum segment_type       type;
    sci_local_segment_t     lsegment;
    sci_remote_segment_t    rsegment;
};
#endif /* _SISCI */



/* Convenience defines */
#define container(mapping) \
    ((struct map*) (((unsigned char*) (mapping)) - offsetof(struct map, handle)))

#define n_ctrl_pages(ctrl, page_size, n_pages) \
    (((page_size) * (n_pages)) / (ctrl)->page_size)





/*
 * Helper function to lock pages and retrieve IO addresses for a 
 * virtual memory range.
 */
static int map_memory(int ioctl_fd, bool devptr, uint64_t vaddr_start, size_t n_pages, uint64_t* ioaddrs)
{
    enum nvm_ioctl_type type;

#ifdef _CUDA
    type = devptr ? NVM_MAP_DEVICE_MEMORY : NVM_MAP_HOST_MEMORY;
#else
    if (devptr != 0)
    {
        return EINVAL;
    }

    type = NVM_MAP_HOST_MEMORY;
#endif

    struct nvm_ioctl_map request = {
        .vaddr_start = vaddr_start,
        .n_pages = n_pages,
        .ioaddrs = ioaddrs
    };

    int err = ioctl(ioctl_fd, type, &request);
    if (err < 0)
    {
        dprintf("Page mapping kernel request failed: %s\n", strerror(errno));
        return errno;
    }
    
    return 0;
}



/*
 * Release locked pages.
 */
static int unmap_memory(int ioctl_fd, uint64_t vaddr_start)
{
    int err = ioctl(ioctl_fd, NVM_UNMAP_MEMORY, &vaddr_start);

    if (err < 0)
    {
        dprintf("Page unmapping kernel request failed: %s\n", strerror(errno));
    }

    return err;
}




/*
 * Allocate map handle container.
 */
static struct map* create_container(const nvm_ctrl_t* ctrl, size_t page_size, size_t n_pages)
{
    // Size of the handle container
    size_t container_size = sizeof(struct map) + (n_ctrl_pages(ctrl, page_size, n_pages)) * sizeof(uint64_t);

    // Allocate handle container
    struct map* container = (struct map*) malloc(container_size);
    if (container == NULL)
    {
        dprintf("Failed to allocate DMA mapping descriptor: %s\n", strerror(errno));
        return NULL;
    }

    container->type = _MAP_TYPE_UNKNOWN;
    container->ioctl_fd = -1;
    container->segment = NULL;
    
    return container;
}



/* 
 * Initialize DMA mapping handle.
 */
static int initialize_handle(nvm_dma_t* map, 
                             const nvm_ctrl_t* ctrl, 
                             void* vaddr, 
                             size_t page_size, 
                             size_t n_pages, 
                             const uint64_t* ioaddrs)
{
    size_t i_page;
    size_t ctrl_page_size = ctrl->page_size;

    // Check if the supplied memory window aligns with controller pages
    if ( (page_size * n_pages) % ctrl_page_size != 0 )
    {
        dprintf("Addresses do not align with controller pages");
        return ERANGE;
    }

    // Set handle members
    map->vaddr = vaddr;
    map->page_size = ctrl->page_size;
    map->n_ioaddrs = n_ctrl_pages(ctrl, page_size, n_pages);

    // Calculate logical page addresses
    for (i_page = 0; i_page < map->n_ioaddrs; ++i_page)
    {
        size_t current_page = (i_page * ctrl_page_size) / page_size;
        size_t offset_within_page = (i_page * ctrl_page_size) % page_size;

        map->ioaddrs[i_page] = ioaddrs[current_page] + offset_within_page;
    }

    return 0;
}



int nvm_dma_map(nvm_dma_t** map, const nvm_ctrl_t* ctrl, void* vaddr, size_t page_size, size_t n_pages, const uint64_t* ioaddrs)
{
    int err;
    *map = NULL;

    // Create handle container
    struct map* container = create_container(ctrl, page_size, n_pages);
    if (container == NULL)
    {
        return ENOMEM;
    }

    // Initialize DMA window handle
    err = initialize_handle(&container->handle, ctrl, vaddr, page_size, n_pages, ioaddrs);
    if (err != 0)
    {
        free(container);
        return err;
    }

    container->type = _MAP_TYPE_UNKNOWN;
    *map = &container->handle;
    return 0;
}



void nvm_dma_unmap(nvm_dma_t* map)
{
    if (map != NULL)
    {
        struct map* container = container(map);

        switch (container->type)
        {
            case _MAP_TYPE_UNKNOWN:
                /* Do nothing */
                break;

#ifdef _SISCI
            case _MAP_TYPE_SMARTIO:
                break;
#endif

            case _MAP_TYPE_IOCTL_HOST:
                break;

#ifdef _CUDA
            case _MAP_TYPE_IOCTL_DEVICE:
                break;
#endif

            default:
                dprintf("Unknown DMA mapping type");
                break;
        }

        free(container);
    }
}




//
//
//
//
//
//static int initialize_and_map(nvm_dma_t* window, nvm_ctrl_t ctrl, int new_ioctl_fd, int devptr, size_t page_size, void* vaddr, size_t size)
//{
//    vaddr = (void*) DMA_ALIGN((uint64_t) vaddr, page_size);
//    size = DMA_SIZE(size, page_size);
//    size_t n_pages = size / page_size;
//
//    uint64_t* ioaddrs = calloc(n_pages, sizeof(uint64_t));
//    if (ioaddrs == NULL)
//    {
//        return ENOMEM;
//    }
//    
//    // Pin pages to memory and get IO addresses
//    int err = map_memory(new_ioctl_fd, devptr, (uint64_t) vaddr, n_pages, ioaddrs);
//    if (err != 0)
//    {
//        free(ioaddrs);
//        return err;
//    }
//
//    err = nvm_dma_window_init(window, ctrl, vaddr, page_size, n_pages, ioaddrs);
//    free(ioaddrs);
//    if (err != 0)
//    {
//        return err;
//    }
//
//    struct handle_container* container = get_container(*window);
//    container->ioctl_fd = new_ioctl_fd;
//
//    return 0;
//}
//
//
//
//int nvm_dma_window_host_map(nvm_dma_t* window, nvm_ctrl_t ctrl, void* vaddr, size_t size)
//{
//    *window = NULL;
//
//    if (vaddr == NULL || size == 0)
//    {
//        return EINVAL;
//    }
//
//    // Get RAM page size
//    long page_size = sysconf(_SC_PAGESIZE);
//    if (page_size < 0)
//    {
//        dprintf("Unable to retrieve page size: %s\n", strerror(errno));
//        return EINVAL;
//    }
//
//    // Get ioctl descriptor
//    int ioctl_fd = _nvm_ioctl_fd_from_ctrl(ctrl);
//    if (ioctl_fd < 0)
//    {
//        return EINVAL;
//    }
//
//    // Duplicate file descriptor
//    int new_ioctl_fd = dup(ioctl_fd);
//    if (ioctl_fd < 0)
//    {
//        dprintf("Failed to copy ioctl handle: %s\n", strerror(errno));
//        return EBADF;
//    }
//
//    int err = initialize_and_map(window, ctrl, new_ioctl_fd, 0, page_size, vaddr, size);
//    if (err != 0)
//    {
//        close(new_ioctl_fd);
//        return err;
//    }
//
//    return 0;
//}
//
//
//
//int nvm_dma_window_device_map(nvm_dma_t* window, nvm_ctrl_t ctrl, void* vaddr, size_t size)
//{
//    *window = NULL;
//
//    if (vaddr == NULL || size == 0)
//    {
//        return EINVAL;
//    }
//
//    size_t page_size = (1ULL << 16);
//
//    // Get ioctl descriptor
//    int ioctl_fd = _nvm_ioctl_fd_from_ctrl(ctrl);
//    if (ioctl_fd < 0)
//    {
//        return EINVAL;
//    }
//
//    // Duplicate file descriptor
//    int new_ioctl_fd = dup(ioctl_fd);
//    if (ioctl_fd < 0)
//    {
//        dprintf("Failed to copy ioctl handle: %s\n", strerror(errno));
//        return EBADF;
//    }
//
//    int err = initialize_and_map(window, ctrl, new_ioctl_fd, 1, page_size, vaddr, size);
//    if (err != 0)
//    {
//        close(new_ioctl_fd);
//        return err;
//    }
//
//    return 0;
//}
//
//
//
//#ifdef _SISCI
//
//int nvm_dis_dma_window_init(nvm_dma_t* window, nvm_ctrl_t ctrl, uint32_t adapter, sci_local_segment_t segment, void* vaddr, size_t size)
//{
//    sci_error_t err;
//    sci_ioaddr_t addr;
//
//    *window = NULL;
//
//    // Round size argument up to nearest controller page-aligned size
//    size = DMA_SIZE(size, ctrl->page_size);
//    if (size == 0)
//    {
//        return EINVAL;
//    }
//
//    // Retrieve SmartIO device reference
//    const struct nvm_device* dev_ref = _nvm_dev_from_ctrl(ctrl);
//    if (dev_ref == NULL)
//    {
//        dprintf("Controller was not initialized with a cluster device");
//        return EINVAL;
//    }
//
//    // Create segment map descriptor
//    struct mapped_segment* mapped_seg = (struct mapped_segment*) malloc(sizeof(struct mapped_segment));
//    if (mapped_seg == NULL)
//    {
//        dprintf("Failed to allocate descriptor: %s\n", strerror(errno));
//        return ENOMEM;
//    }
//
//    mapped_seg->segment = segment;
//
//    if (_nvm_dev_get(&mapped_seg->device, dev_ref->device_id, adapter) != 0)
//    {
//        dprintf("Failed to get device reference");
//        return ENODEV;
//    }
//
//    // Create handle container
//    struct handle_container* container = create_container(ctrl, size, 1);
//    if (container == NULL)
//    {
//        _nvm_dev_put(&mapped_seg->device);
//        free(mapped_seg);
//        return ENOMEM;
//    }
//
//    // Do the actual reverse mapping
//    SCIMapSegmentForDevice(segment, mapped_seg->device.device, mapped_seg->device.adapter, &addr, 0, &err);
//    if (err != SCI_ERR_OK)
//    {
//        dprintf("Failed to map DMA window for controller: %s\n", SCIGetErrorString(err));
//        _nvm_dev_put(&mapped_seg->device);
//        free(mapped_seg);
//        return EIO;
//    }
//
//    // Initialize DMA window handle
//    initialize_handle(&container->window, ctrl, vaddr, size, 1, (uint64_t*) &addr);
//
//    container->mapped_seg = mapped_seg;
//    *window = &container->window;
//    return 0;
//}
//
//#endif
//
//
//void nvm_dma_window_free(nvm_dma_t window)
//{
//    if (window != NULL)
//    {
//        struct handle_container* container = get_container(window);
//
//#ifdef _SISCI
//        if (container->mapped_seg != NULL)
//        {
//            sci_error_t err;
//            struct nvm_device* device = &container->mapped_seg->device;
//            
//            SCIUnmapSegmentForDevice(container->mapped_seg->segment, device->device, device->adapter, 0, &err);
//            if (err != SCI_ERR_OK)
//            {
//                dprintf("Failed to unmap DMA window for controller: %s\n", SCIGetErrorString(err));
//            }
//
//            _nvm_dev_put(&container->mapped_seg->device);
//            free(container->mapped_seg);
//        }
//#endif
//        
//        if (container->ioctl_fd >= 0)
//        {
//            unmap_memory(container->ioctl_fd, (uint64_t) window->vaddr);
//            close(container->ioctl_fd);
//        }
//
//        free(container);
//    }
//}
//
