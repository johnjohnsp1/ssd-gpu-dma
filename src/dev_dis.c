#ifndef _SISCI
#error "Must compile with SISCI support"
#endif

#ifndef __DIS_CLUSTER__
#define __DIS_CLUSTER__
#endif

#include <sisci_types.h>
#include <sisci_error.h>
#include <sisci_api.h>
#include <nvm_types.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include "dev.h"
#include "dprintf.h"



/*
 * Helper function to borrow a SmartIO device and initialize a reference.
 */
static int borrow_device(struct device* dev, uint64_t dev_id, uint32_t adapter)
{
    sci_error_t err;

    dev->device_id = dev_id;
    dev->adapter = adapter;

    SCIOpen(&dev->sd, 0, &err);
    if (err != SCI_ERR_OK)
    {
        dprintf("Failed to create virtual device: %s\n", SCIGetErrorString(err));
        free(dev);
        return EIO;
    }

    SCIBorrowDevice(dev->sd, &dev->device, dev_id, 0, &err);
    if (err != SCI_ERR_OK)
    {
        dprintf("Failed to increase device reference: %s\n", SCIGetErrorString(err));
        SCIClose(dev->sd, 0, &err);
        free(dev);
        return ENODEV;
    }

    return 0;
}



/*
 * Helper function to return a SmartIO device.
 */
static void return_device(struct device* dev)
{
    sci_error_t err;

    if (dev != NULL)
    {
        SCIReturnDevice(dev->device, 0, &err);
        SCIClose(dev->sd, 0, &err);
    }
}



/*
 * Helper function to connect to a segment.
 */
static int connect_segment(struct device_memory* mem, const struct device* dev, uint32_t segment_no, size_t size, uint32_t flags)
{
    sci_error_t err;
    int status;

    status = borrow_device(&mem->device, dev->device_id, dev->adapter);
    if (status != 0)
    {
        goto leave;
    }

    const struct device* d = &mem->device;
    mem->segment_no = segment_no;
    mem->size = size;
    mem->vaddr = NULL;

    SCIConnectDeviceMemory(d->sd, &mem->segment, d->adapter, d->device, segment_no, 0, flags, &err);
    if (err != SCI_ERR_OK)
    {
        dprintf("Failed to connect to device memory: %s\n", SCIGetErrorString(err));
        status = ENODEV;
        goto release;
    }

    if (size > 0)
    {
        mem->vaddr = SCIMapRemoteSegment(mem->segment, &mem->map, 0, size, NULL, SCI_FLAG_IO_MAP_IOSPACE, &err);
        if (err != SCI_ERR_OK)
        {
            dprintf("Failed to map device memory into address space: %s\n", SCIGetErrorString(err));
            status = EIO;
            goto disconnect;
        }
    }

    return 0;

disconnect:
    do
    {
        SCIDisconnectSegment(mem->segment, 0, &err);
    }
    while (err == SCI_ERR_BUSY);

    if (err != SCI_ERR_OK)
    {
        dprintf("Failed to disconnect from device memory: %s\n", SCIGetErrorString(err));
    }
    
release:
    return_device(&mem->device);
leave:
    return status;
}



/*
 * Helper function to disconnect from a remote segment.
 */
static void disconnect_segment(struct device_memory* mem)
{
    sci_error_t err = SCI_ERR_OK;

    if (mem == NULL || mem->vaddr == NULL)
    {
        return;
    }

    do
    {
        SCIUnmapSegment(mem->map, 0, &err);
    }
    while (err == SCI_ERR_BUSY);

#ifndef NDEBUG
    if (err != SCI_ERR_OK)
    {
        dprintf("Failed to unmap device memory: %s\n", SCIGetErrorString(err));
    }
#endif

    do
    {
        SCIDisconnectSegment(mem->segment, 0, &err);
    }
    while (err == SCI_ERR_BUSY);

#ifndef NDEBUG
    if (err != SCI_ERR_OK)
    {
        dprintf("Failed to disconnect from device memory: %s\n", SCIGetErrorString(err));
    }
#endif

    return_device(&mem->device);
}



/* 
 * Acquire a device reference.
 */
int _nvm_device_get(struct device** handle, uint64_t dev_id, uint32_t adapter)
{
    *handle = NULL;

    struct device* dev = (struct device*) malloc(sizeof(struct device));
    if (dev == NULL)
    {
        dprintf("Failed to allocate device reference: %s\n", strerror(errno));
        return ENOMEM;
    }

    int err = borrow_device(dev, dev_id, adapter);
    if (err != 0)
    {
        free(dev);
        return err;
    }
    
    *handle = dev;
    return 0;
}



/*
 * Release device reference.
 */
void _nvm_device_put(struct device* dev)
{
    if (dev != NULL)
    {
        return_device(dev);
        free(dev);
    }
}



/*
 * Duplicate device reference.
 */
int _nvm_device_dup(struct device** handle, const struct device* dev)
{
    if (dev != NULL)
    {
        return _nvm_device_get(handle, dev->device_id, dev->adapter);
    }

    return EINVAL;
}



/*
 * Connect to device memory.
 */
int _nvm_device_memory_get(struct device_memory** handle,  
                           const struct device* dev, 
                           uint32_t segment_no,
                           size_t size,
                           enum memory_kind kind)
{
    *handle = NULL;

    struct device_memory* mem = (struct device_memory*) malloc(sizeof(struct device_memory));
    if (mem == NULL)
    {
        dprintf("Failed to allocate device memory reference: %s\n", strerror(errno));
        return ENOMEM;
    }

    uint32_t flags = 0;
    switch (kind)
    {
        case _DEVICE_MEMORY_BAR:
            flags = SCI_FLAG_BAR;
            break;

        case _DEVICE_MEMORY_SHARED:
            flags = SCI_FLAG_SHARED;
            break;

        case _DEVICE_MEMORY_PRIVATE:
            flags = SCI_FLAG_PRIVATE;
            break;

#ifndef NDEBUG
        default:
            dprintf("Unknown device memory kind\n");
            free(mem);
            return EINVAL;
#endif
    }

    int err = connect_segment(mem, dev, segment_no, size, flags);
    if (err != 0)
    {
        free(mem);
        return err;
    }

    return 0;
}



/*
 * Disconnect from device memory.
 */
void _nvm_device_memory_put(struct device_memory* mem)
{
    if (mem != NULL)
    {
        disconnect_segment(mem);
        free(mem);
    }
}

