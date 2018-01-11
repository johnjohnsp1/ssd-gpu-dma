#ifndef __NVM_INTERNAL_DIS_INTERRUPT_H__
#define __NVM_INTERNAL_DIS_INTERRUPT_H__

/* Forward declarations */
struct local_intr;
struct remote_intr;


#ifdef _SISCI

/* Make sure everything is defined as needed */
#ifndef __DIS_CLUSTER__
#define __DIS_CLUSTER__
#endif

/* Necessary includes */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <sisci_types.h>


/*
 * Interrupt callback.
 */
typedef void (*interrupt_cb_t)(void* user_data, const void* recv_data, uint16_t recv_len);



/*
 * Local interrupt descriptor.
 * Data must be free'd manually.
 */
struct interrupt
{
    sci_desc_t                  sd;         // SISCI virtual device descriptor
    sci_local_data_interrupt_t  intr;       // SISCI data interrupt handle
    uint32_t                    intr_no;    // Interrupt number
    uint32_t                    node_id;    // DIS node identifier
    void*                       data;       // User data
    interrupt_cb_t              callback;   // Interrupt callback
};



/*
 * Create a local data interrupt.
 */
int _nvm_interrupt_get(struct interrupt* intr, 
                       uint32_t adapter, 
                       void* cb_data, 
                       interrupt_cb_t cb_func);



/*
 * Remove a local data interrupt.
 */
void _nvm_interrupt_put(struct interrupt* intr);



/*
 * Block for a duration while waiting for an interrupt and removes interrupt afterwards.
 * Returns success if length of received data matches expected length.
 */
int _nvm_interrupt_wait(struct interrupt* intr, void* data, uint32_t expected, uint32_t timeout);


#endif /* _SISCI */
#endif /* __NVM_INTERNAL_DIS_INTERRUPT_H__ */
