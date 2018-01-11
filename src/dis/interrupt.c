#ifndef _SISCI
#error "Must compile with SISCI support"
#endif

#ifndef __DIS_CLUSTER__
#define __DIS_CLUSTER__
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include "dis/interrupt.h"
#include "dprintf.h"
#include <sisci_types.h>
#include <sisci_error.h>
#include <sisci_api.h>



static sci_callback_action_t interrupt_callback(struct interrupt* intr_data, 
                                                sci_local_data_interrupt_t intr,
                                                void* data,
                                                uint32_t length,
                                                sci_error_t status)
{
    if (status != SCI_ERR_OK)
    {
        dprintf("Unexpected status in interrupt handler routine: %s\n", SCIGetErrorString(status));
        return SCI_CALLBACK_CANCEL;
    }

    if (intr != intr_data->intr)
    {
        dprintf("Possible memory corruption\n");
        return SCI_CALLBACK_CANCEL;
    }

    intr_data->callback(intr_data->data, data, length);

    return SCI_CALLBACK_CONTINUE;
}



int _nvm_interrupt_get(struct interrupt* intr, uint32_t adapter, void* cb_data, interrupt_cb_t cb)
{
    sci_error_t err = SCI_ERR_OK;

    // Get local node identifier
    SCIGetLocalNodeId(adapter, &intr->node_id, 0, &err);
#ifndef NDEBUG
    if (err != SCI_ERR_OK)
    {
        dprintf("Unexpected error: %s\n", SCIGetErrorString(err));
        return EIO;
    }
#endif

    // Open SISCI descriptor
    SCIOpen(&intr->sd, 0, &err);
#ifndef NDEBUG
    if (err != SCI_ERR_OK)
    {
        dprintf("Failed to open SISCI virtual device: %s\n", SCIGetErrorString(err));
        return EIO;
    }
#endif

    intr->data = cb_data;
    intr->callback = cb;
    
    uint32_t flags = 0;
    void* data = NULL;
    sci_cb_data_interrupt_t callback = NULL;

    // Callback was supplied, set up parameters
    if (cb != NULL)
    {
        data = (void*) intr;
        callback = (sci_cb_data_interrupt_t) interrupt_callback;
        flags |= SCI_FLAG_USE_CALLBACK;
    }

    // Create data interrupt
    SCICreateDataInterrupt(intr->sd, &intr->intr, adapter, &intr->intr_no, callback, data, flags, &err);
    if (err != SCI_ERR_OK)
    {
        dprintf("Failed to create data interrupt: %s\n", SCIGetErrorString(err));
        SCIClose(intr->sd, 0, &err);
        return ENOSPC;
    }

    return 0;
}



void _nvm_interrupt_put(struct interrupt* intr)
{
    sci_error_t err = SCI_ERR_OK;

    do
    {
        SCIRemoveDataInterrupt(intr->intr, 0, &err);
    }
    while (err == SCI_ERR_BUSY);

    SCIClose(intr->sd, 0, &err);
}



int _nvm_interrupt_wait(struct interrupt* intr, void* data, uint32_t expected, uint32_t timeout)
{
    sci_error_t err = SCI_ERR_OK;
    uint32_t len = expected;
    
    SCIWaitForDataInterrupt(intr->intr, data, &len, timeout, 0, &err);

    switch (err)
    {
        case SCI_ERR_OK:
            if (len != expected)
            {
                return EBADE;
            }
            return 0;

        case SCI_ERR_TIMEOUT:
            return ETIMEDOUT;

        default:
            dprintf("Waiting for data interrupt unexpectedly failed: %s\n", SCIGetErrorString(err));
            return EIO;
    }
}

