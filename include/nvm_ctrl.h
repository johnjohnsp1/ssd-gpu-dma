#ifndef __NVM_CTRL_H__
#define __NVM_CTRL_H__
#ifdef __cplusplus
extern "C" {
#endif

#include <nvm_types.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __DIS_CLUSTER__
#include <sisci_types.h>
#endif



/* 
 * Minimum size of mapped controller memory.
 */
#define NVM_CTRL_MEM_MINSIZE                        0x2000



/*
 * Initialize NVM controller handle.
 *
 * Read from controller registers and initialize controller handle. 
 * This function should be used when using the kernel module or to manually
 * read from sysfs.
 */
int nvm_ctrl_init(nvm_ctrl_t** ctrl, const char* filename);



/* 
 * Initialize NVM controller handle.
 *
 * Read from controller registers and initialize the controller handle using
 * a memory-mapped pointer to the PCI device BAR.
 *
 * This function should be used when neither SmartIO nor the disnvme kernel
 * module are used.
 *
 * Note: ctrl_mem must be at least NVM_CTRL_MEM_MINSIZE large and mapped
 *       as IO memory. See arguments for mmap() for more info.
 */
int nvm_raw_ctrl_init(nvm_ctrl_t** ctrl, volatile void* mm_ptr, size_t mm_size);



/*
 * Release controller handle.
 */
void nvm_ctrl_free(nvm_ctrl_t* ctrl);



/* 
 * Reset NVM controller.
 *
 * The queue memory must be memset to zero and be exactly one page size large.
 * IO addresses must align to the controller page size. 
 *
 * Note: The controller must be unbound from any driver before attempting to
 *       reset the controller.
 *
 * Note: This function is implicitly called by the controller manager, so it
 *       should not be necessary to call it directly.
 */
int nvm_raw_ctrl_reset(const nvm_ctrl_t* ctrl, uint64_t acq_ioaddr, uint64_t asq_ioaddr);



#ifdef __DIS_CLUSTER__

/* 
 * Initialize NVM controller handle.
 *
 * Read from device registers and initialize controller handle. 
 * This function should be used when SmartIO is being used.
 */
int nvm_dis_ctrl_init(nvm_ctrl_t** ctrl, uint64_t smartio_dev_id, uint32_t dis_adapter);



/* 
 * Retrieve SmartIO device reference.
 *
 * Get the SmartIO device reference used internally for connecting to the 
 * physical controller. 
 *
 * The reference will be the same reference as used internally, which
 * means that if the controller handle is destroyed, the reference will no
 * longer be valid.
 */
int nvm_dis_ctrl_device(const nvm_ctrl_t* ctrl, sci_device_t* dev);



/*
 * Get segment local to the controller.
 *
 * Create a segment local to the controller device and retrieve a remote 
 * remote segment reference. The remote segment can then be passed to the 
 * nvm_dis_dma_map_remote() function.
 *
 * Note: the caller must also call the corresponding put function.
 */
//int nvm_dis_ctrl_get_segment(const nvm_ctrl_t* ctrl, 
//                             sci_remote_segment_t* segment, 
//                             uint32_t segment_no, 
//                             bool shared);
//
//
//void nvm_dis_ctrl_put_segment(const nvm_ctrl_t* ctrl, sci_remote_segment_t segment);

#endif


#ifdef __cplusplus
}
#endif
#endif /* __NVM_CTRL_H__ */
