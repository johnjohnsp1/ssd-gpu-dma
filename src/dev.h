#ifndef __NVM_INTERNAL_DEVICE_H__
#define __NVM_INTERNAL_DEVICE_H__

/* Forward declarations */
struct device;
struct device_memory;


#ifdef _SISCI

#include <nvm_types.h>
#include <sisci_types.h>
#include <stddef.h>
#include <stdint.h>



/* 
 * SmartIO device descriptor.
 *
 * Holds a "borrowed" reference to the device.
 * Handles that require a device or controller reference should take this.
 */
struct device
{
    sci_desc_t              sd;             // SISCI virtual device descriptor
    uint64_t                device_id;      // SISCI SmartIO device identifier
    uint32_t                adapter;        // DIS adapter number
    sci_device_t            device;         // SmartIO device handle
};



/*
 * SmartIO device memory segment descriptor.
 *
 * Describes mapping to device-local memory, e.g. PCI BARs or memory residing 
 * on the same host as the device.
 */
struct device_memory
{
    struct device           device;         // Device reference
    uint32_t                segment_no;     // Device segment number
    sci_remote_segment_t    segment;        // SISCI remote segment to device memory
    sci_map_t               map;            // SISCI memory map handle
    volatile void*          vaddr;          // Mapped memory
    size_t                  size;           // Size of mapped memory
};



/*
 * Device memory kind.
 * Indicates the type of device memory.
 */
enum memory_kind
{
    _DEVICE_MEMORY_BAR      = 0x00,         // Remote segment is PCI device BAR
    _DEVICE_MEMORY_PRIVATE  = 0x01,         // Remote segment is private
    _DEVICE_MEMORY_SHARED   = 0x02          // Remote segment is shared
};



/*
 * Acquire device reference.
 */
int _nvm_device_get(struct device** handle, uint64_t dev_id, uint32_t adapter);



/*
 * Release device reference.
 */
void _nvm_device_put(struct device* dev);



/*
 * Duplicate device reference.
 */
int _nvm_device_dup(struct device** handle, const struct device* dev);



/*
 * Connect to device memory.
 */
int _nvm_device_memory_get(struct device_memory** handle, 
                           const struct device* dev, 
                           uint32_t segment_no,
                           size_t size,
                           enum memory_kind kind);



/*
 * Disconnect from device memory.
 */
void _nvm_device_memory_put(struct device_memory* dev_mem);



#endif /* _SISCI */
#endif /* __NVM_INTERNAL_DEVICE_H__ */
