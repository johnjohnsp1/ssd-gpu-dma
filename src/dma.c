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
#include "dis/map.h"
#include "dis/local.h"
#include "dis/device.h"
#include "ctrl.h"
#include "util.h"
#include "regs.h"
#include "dprintf.h"

/* Forward declarations */
struct smartio_map;



/* 
 * Mapping handle type.
 */
enum dma_type
{
    _DMA_TYPE_MANUAL        = 0x00,     // Mapping is manual / unknown
    _DMA_TYPE_SMARTIO       = 0x01,     // Mapping is for a SISCI segment
    _DMA_TYPE_IOCTL_HOST    = 0x02,     // Mapping is for host memory
    _DMA_TYPE_IOCTL_DEVICE  = 0x04      // Mapping is for CUDA device memory
};



/*
 * Internal handle container.
 */
struct dma_container
{
    enum dma_type           type;       // Handle type
    int                     ioctl_fd;   // File descriptor to kernel module
    struct smartio_map*     mapping;    // SmartIO mapping descriptor
    nvm_dma_t               handle;     // DMA mapping descriptor
} __attribute__((aligned (32)));



#ifdef _SISCI
/*
 * SmartIO mapping descriptor.
 * 
 * Holds a device reference and mapping descriptors.
 */
struct smartio_map
{
    struct device           device;     // Device reference
    struct va_map           va_mapping; // Virtual address mapping
    struct io_map           io_mapping; // Mapping for device
};
#endif /* _SISCI */



/* Get handle container */
#define container(m) \
    ((struct dma_container*) (((unsigned char*) (m)) - offsetof(struct dma_container, handle)))


#define n_ctrl_pages(ctrl, page_size, n_pages) \
    (((page_size) * (n_pages)) / (ctrl)->page_size)



/*
 * Allocate handle container.
 */
static struct dma_container* create_container(const nvm_ctrl_t* ctrl, size_t page_size, size_t n_pages)
{
    // Size of the handle container
    size_t container_size = sizeof(struct dma_container) + (n_ctrl_pages(ctrl, page_size, n_pages)) * sizeof(uint64_t);

    // Allocate handle container
    struct dma_container* container = (struct dma_container*) malloc(container_size);

    if (container == NULL)
    {
        dprintf("Failed to allocate DMA mapping descriptor: %s\n", strerror(errno));
        return NULL;
    }

    // Clear container
    container->type = _DMA_TYPE_MANUAL;
    container->ioctl_fd = -1;
    container->mapping = NULL;
    
    return container;
}



/* 
 * Initialize the mapping handle.
 * Sets handle members and populates address list.
 */
static int initialize_handle(nvm_dma_t* handle, 
                             const nvm_ctrl_t* ctrl, 
                             volatile void* vaddr, 
                             size_t page_size, 
                             size_t n_pages, 
                             const uint64_t* ioaddrs)
{
    size_t i_page;
    size_t ctrl_page_size = ctrl->page_size;

    handle->n_ioaddrs = 0;
    handle->page_size = 0;

    // Check if the supplied memory window aligns with controller pages
    if ( (page_size * n_pages) % ctrl_page_size != 0 )
    {
        dprintf("Addresses do not align with controller pages");
        return ERANGE;
    }

    // Set handle members
    handle->vaddr = (void*) vaddr;
    handle->page_size = ctrl->page_size;
    handle->n_ioaddrs = n_ctrl_pages(ctrl, page_size, n_pages);

    // Calculate logical page addresses
    for (i_page = 0; i_page < handle->n_ioaddrs; ++i_page)
    {
        size_t current_page = (i_page * ctrl_page_size) / page_size;
        size_t offset_within_page = (i_page * ctrl_page_size) % page_size;

        handle->ioaddrs[i_page] = ioaddrs[current_page] + offset_within_page;
    }

    return 0;
}


#ifdef _SISCI
/*
 * Create SmartIO mapping descriptor and take device reference.
 */
static struct smartio_map* create_mapping(const struct device* dev)
{
    // Allocate mapping descriptor
    struct smartio_map* map = (struct smartio_map*) malloc(sizeof(struct smartio_map));
    if (map == NULL)
    {
        dprintf("Failed to allocate mapping descriptor: %s\n", strerror(errno));
        return NULL;
    }

    // Take device reference
    int err = _nvm_device_get(&map->device, dev->device_id);
    if (err != 0)
    {
        free(map);
        dprintf("Failed to take device reference: %s\n", strerror(err));
        return NULL;
    }

    // Clear mapping descriptors
    VA_MAP_CLEAR(&map->va_mapping);
    IO_MAP_CLEAR(&map->io_mapping);

    return map;
}
#endif



#ifdef _SISCI
/*
 * Unmap mappings, release device reference and delete SmartIO mapping descriptor.
 */
static void remove_mapping(struct smartio_map* map)
{
    if (map != NULL)
    {
        _nvm_va_unmap(&map->va_mapping);
        _nvm_io_unmap(&map->io_mapping);
        _nvm_device_put(&map->device);
        free(map);
    }
}
#endif



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
 * Create DMA mapping descriptor from physical/bus addresses.
 */
int nvm_dma_map(nvm_dma_t** handle, const nvm_ctrl_t* ctrl, void* vaddr, size_t page_size, size_t n_pages, const uint64_t* ioaddrs)
{
    int err;
    *handle = NULL;

    // Create handle container
    struct dma_container* container = create_container(ctrl, page_size, n_pages);
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

    *handle = &container->handle;
    return 0;
}



#ifdef _SISCI
int nvm_dis_dma_map_local(nvm_dma_t** handle, 
                          const nvm_ctrl_t* ctrl, 
                          uint32_t adapter, 
                          sci_local_segment_t segment,
                          size_t size,
                          bool map_va)
{
    size = NVM_CTRL_ALIGN(ctrl, size);
    *handle = NULL;

    // Get device reference from controller
    const struct device* dev = _nvm_device_from_ctrl(ctrl);
    if (dev == NULL)
    {
        dprintf("Controller is not a cluster device\n");
        return EINVAL;
    }

    // Create mapping descriptor
    struct smartio_map* md = create_mapping(dev);
    if (md == NULL)
    {
        return ENOMEM;
    }

    // Set reverse mapping (device-local mapping)
    int err = _nvm_io_map_local(&md->io_mapping, dev->device, segment, adapter);
    if (err != 0)
    {
        remove_mapping(md);
        return err;
    }

    // Set up virtual address space mapping
    if (map_va)
    {
        err = _nvm_va_map_local(&md->va_mapping, size, segment);
        if (err != 0)
        {
            remove_mapping(md);
            return err;
        }
    }

    // Create handle container
    struct dma_container* container = create_container(ctrl, size, 1);
    if (container == NULL)
    {
        remove_mapping(md);
        return ENOMEM;
    }

    container->type = _DMA_TYPE_SMARTIO;
    container->mapping = md;

    // Initialize DMA handle
    initialize_handle(&container->handle, ctrl, md->va_mapping.vaddr, size, 1, 
            (uint64_t*) &md->io_mapping.ioaddr);

    *handle = &container->handle;
    return 0;
}
#endif /* _SISCI */



#ifdef _SISCI
int nvm_dis_dma_map_remote(nvm_dma_t** handle, 
                           const nvm_ctrl_t* ctrl, 
                           sci_remote_segment_t segment, 
                           bool map_va, 
                           bool map_wc)
{
    *handle = NULL;

    // Find segment size
    size_t size = SCIGetRemoteSegmentSize(segment) & NVM_PAGE_MASK(ctrl->page_size);

    // Get device reference from controller
    const struct device* dev = _nvm_device_from_ctrl(ctrl);
    if (dev == NULL)
    {
        dprintf("Controller is not a cluster device\n");
        return EINVAL;
    }

    // Create mapping descriptor
    struct smartio_map* md = create_mapping(dev);
    if (md == NULL)
    {
        return ENOMEM;
    }

    // Set up device-local mapping
    int err = _nvm_io_map_remote(&md->io_mapping, dev->device, segment);
    if (err != 0)
    {
        remove_mapping(md);
        return err;
    }

    // Map into local address space
    if (map_va)
    {
        err = _nvm_va_map_remote(&md->va_mapping, size, segment, true, map_wc);
        if (err != 0)
        {
            remove_mapping(md);
            return err;
        }
    }

    // Create handle container
    struct dma_container* container = create_container(ctrl, size, 1);
    if (container == NULL)
    {
        remove_mapping(md);
        return ENOMEM;
    }

    container->type = _DMA_TYPE_SMARTIO;
    container->mapping = md;

    // Initialize the handle
    initialize_handle(&container->handle, ctrl, md->va_mapping.vaddr, size, 1, 
            (uint64_t*) &md->io_mapping.ioaddr);

    *handle = &container->handle;
    return 0;
}
#endif /* _SISCI */



/*
 * Remove DMA mapping descriptor.
 */
void nvm_dma_unmap(nvm_dma_t* handle)
{
    if (handle == NULL)
    {
        return;
    }

    struct dma_container* outer = container(handle);

    switch (outer->type)
    {
        case _DMA_TYPE_MANUAL:
            // Do nothing
            break;

#ifdef _SISCI
        case _DMA_TYPE_SMARTIO:
            remove_mapping(outer->mapping);
            break;
#endif

        case _DMA_TYPE_IOCTL_HOST:
                break;

#ifdef _CUDA
        case _DMA_TYPE_IOCTL_DEVICE:
                break;
#endif

        default:
            dprintf("Unknown DMA mapping type for handle at address %p\n", handle);
            break;
    }

    free(outer);
}




#ifdef _SISCI
/*
 * Create local segment and map it.
 */
int nvm_dis_dma_create(nvm_dma_t** handle, const nvm_ctrl_t* ctrl, uint32_t adapter, uint32_t id, size_t size)
{
    return ENOTSUP;
}
#endif



#ifdef _SISCI
/*
 * Connect to device memory.
 */
int nvm_dis_dma_connect(nvm_dma_t** handle, const nvm_ctrl_t* ctrl, uint32_t segno, size_t size, bool shared)
{
    return ENOTSUP;
}
#endif

