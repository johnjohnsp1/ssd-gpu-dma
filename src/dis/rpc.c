#ifndef _SISCI
#error "Must compile with SISCI support"
#endif

#ifndef __DIS_CLUSTER__
#define __DIS_CLUSTER__
#endif

#include <nvm_types.h>
#include <nvm_rpc.h>
#include <nvm_aq.h>
#include <nvm_util.h>
#include <nvm_queue.h>
#include <nvm_error.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <string.h>
#include "device.h"
#include "rpc.h"
#include "regs.h"
#include "ctrl.h"
#include "dprintf.h"
#include <sisci_types.h>
#include <sisci_error.h>
#include <sisci_api.h>


#define RPC_COMMAND_TIMEOUT     2500
#define RPC_MAGIC_SIGNATURE     0xDEADBEEF


/*
 * RPC command message format.
 */
struct __attribute__((packed)) rpc_cmd
{
    uint32_t                    node_id;    // Initiator node identifier
    uint32_t                    intr_no;    // Callback interrupt number
    unsigned char               cmd[64];    // Command to execute
};



/*
 * RPC completion message format.
 */
struct __attribute__((packed)) rpc_cpl
{
    unsigned char               cmd[64];    // Modified command (zero'd means rejected)
    unsigned char               cpl[16];    // Command completion
};



/*
 * Information about binding handle (exported on shared device memory)
 */
struct __attribute__((packed)) handle_info
{
    uint32_t                    magic;      // Magic signature
    uint32_t                    node_id;    // Node identifier
    uint32_t                    intr_no;    // Interrupt number
};



/*
 * Local RPC binding handle.
 */
struct binding_handle
{
    struct device_memory        dmem;       // Device memory reference
    uint32_t                    intr_no;    // Interrupt number
    nvm_aq_ref                  rpc_ref;    // RPC reference
    nvm_dis_rpc_cb_t            rpc_cb;     // RPC callback
    sci_desc_t                  sd;         // SISCI virtual descriptor
    sci_local_data_interrupt_t  intr;       // SISCI interrupt handle
};



/*
 * Remote RPC binding.
 */
struct binding
{
    struct device_memory        dmem;       // Device memory reference
    uint32_t                    node_id;    // Node identifier
    sci_desc_t                  sd;         // SISCI virtual descriptor
    sci_remote_data_interrupt_t intr;       // SISCI interrupt handle
};



/*
 * Trigger remote interrupt with data.
 */
static int send_data(struct binding* ref, void* data, size_t length)
{
    sci_error_t err;

    SCITriggerDataInterrupt(ref->intr, data, length, 0, &err);
    if (err != SCI_ERR_OK)
    {
        dprintf("Failed to trigger data interrupt\n");
        return EIO;
    }

    return 0;
}



/*
 * Handle remote command request.
 */
static sci_callback_action_t handle_procedure(struct binding_handle* handle, 
                                              sci_local_data_interrupt_t intr, 
                                              struct rpc_cmd* request, 
                                              uint32_t length,
                                              sci_error_t status)
{
    dprintf("Remote invoked\n");
    return SCI_CALLBACK_CONTINUE;
}



/*
 * Initiate remote command request.
 */
static int invoke_procedure(struct binding* binding, nvm_cmd_t* cmd, nvm_cpl_t* cpl)
{
    dprintf("Invoke remote\n");
    return NVM_ERR_PACK(NULL, 0);
}



/*
 * Helper function to create a a server binding handle.
 */
static int create_binding_handle(struct binding_handle** handle, nvm_aq_ref ref, uint32_t adapter, nvm_dis_rpc_cb_t cb)
{
    *handle = NULL;

    const nvm_ctrl_t* ctrl = _nvm_ctrl_from_aq_ref(ref);
    if (ctrl == NULL)
    {
        return EINVAL;
    }

    const struct device* dev = _nvm_device_from_ctrl(ctrl);
    if (dev == NULL)
    {
        return EINVAL;
    }

    struct binding_handle* bh = (struct binding_handle*) malloc(sizeof(struct binding_handle));
    if (bh == NULL)
    {
        dprintf("Failed to allocate RPC binding handle: %s\n", strerror(errno));
        return ENOMEM;
    }

    int status = _nvm_device_memory_get(&bh->dmem, dev, adapter, 0, 
            sizeof(struct handle_info) * NVM_DIS_RPC_MAX_ADAPTER, true, SCI_FLAG_SHARED);
    if (status != 0)
    {
        free(bh);
        return status;
    }

    sci_error_t err;
    uint32_t node_id = 0;
    SCIGetLocalNodeId(adapter, &node_id, 0, &err);

    SCIOpen(&bh->sd, 0, &err);
    if (err != SCI_ERR_OK)
    {
        _nvm_device_memory_put(&bh->dmem);
        free(bh);
        dprintf("Failed to open SISCI descriptor: %s\n", SCIGetErrorString(err));
        return EIO;
    }

    bh->rpc_ref = ref;
    bh->rpc_cb = cb;

    uint32_t flags = SCI_FLAG_USE_CALLBACK;
    sci_cb_data_interrupt_t cb_intr = (sci_cb_data_interrupt_t) handle_procedure;

    SCICreateDataInterrupt(bh->sd, &bh->intr, adapter, &bh->intr_no, cb_intr, bh, flags, &err);
    if (err != SCI_ERR_OK)
    {
        _nvm_device_memory_put(&bh->dmem);
        dprintf("Failed to create binding handle interrupt: %s\n", SCIGetErrorString(err));
        SCIClose(bh->sd, 0, &err);
        free(bh);
        return EIO;
    }

    struct handle_info* info = ((struct handle_info*) bh->dmem.va_mapping.vaddr);
    info[adapter].magic = 0xDEADBEEF;
    info[adapter].node_id = node_id;
    info[adapter].intr_no = bh->intr_no;

    *handle = bh;
    return 0;
}



/*
 * Helper function to remove a server binding handle.
 */
static void remove_binding_handle(struct binding_handle* handle, uint32_t adapter)
{
    sci_error_t err;
    
    struct handle_info* info = ((struct handle_info*) handle->dmem.va_mapping.vaddr);
    info[adapter].magic = 0;
    info[adapter].node_id = 0;
    info[adapter].intr_no = 0;

    SCIRemoveDataInterrupt(handle->intr, 0, &err);
    SCIClose(handle->sd, 0, &err);
    _nvm_device_memory_put(&handle->dmem);
    free(handle);
}



/*
 * Helper function to try to connect to remote interrupt.
 */
static int try_bind(struct binding* binding, size_t max)
{
    sci_error_t err;

    const struct handle_info* info = (const struct handle_info*) binding->dmem.va_mapping.vaddr;
    for (size_t i = 0; i < max; ++i)
    {
        // Read information
        uint32_t magic = info[i].magic;
        uint32_t node_id = info[i].node_id;
        uint32_t intr_no = info[i].intr_no;

        if (magic != RPC_MAGIC_SIGNATURE)
        {
            continue;
        }

        // Attempt to connect
        uint32_t adapter = binding->dmem.adapter;
        SCIConnectDataInterrupt(binding->sd, &binding->intr, node_id, adapter, intr_no, SCI_INFINITE_TIMEOUT, 0, &err);
        if (err == SCI_ERR_OK)
        {
            return 0;
        }
    }

    dprintf("Failed to connect to remote interrupt\n");
    return ECONNREFUSED;
}


/*
 * Helper function to connect to device shared memory,
 * extract handle info and connect to interrupt.
 */
static int create_binding(struct binding** handle, const struct device* dev, uint32_t adapter)
{
    sci_error_t err;
    *handle = NULL;

    struct binding* binding = (struct binding*) malloc(sizeof(struct binding));
    if (binding == NULL)
    {
        dprintf("Failed to allocate binding descriptor: %s\n", strerror(errno));
        return ENOMEM;
    }

    int status = _nvm_device_memory_get(&binding->dmem, dev, adapter, 0, 
            sizeof(struct handle_info) * NVM_DIS_RPC_MAX_ADAPTER, false, SCI_FLAG_SHARED);
    if (status != 0)
    {
        free(binding);
        dprintf("Failed to connect to binding handle information: %s\n", strerror(status));
        return status;
    }

    SCIOpen(&binding->sd, 0, &err);
    if (err != SCI_ERR_OK)
    {
        _nvm_device_memory_put(&binding->dmem);
        free(binding);
        dprintf("Failed to open SISCI descriptor: %s\n", SCIGetErrorString(err));
        return EIO;
    }

    status = try_bind(binding, NVM_DIS_RPC_MAX_ADAPTER);
    if (status != 0)
    {
        SCIClose(binding->sd, 0, &err);
        _nvm_device_memory_put(&binding->dmem);
        free(binding);
        return status;
    }

    *handle = binding;
    return 0;
}



/*
 * Helper function to disconnect from remote interrupt and 
 * shared device memory.
 */
static void remove_binding(struct binding* binding)
{
    sci_error_t err;

    do
    {
        SCIDisconnectDataInterrupt(binding->intr, 0, &err);
    }
    while (err == SCI_ERR_BUSY);

    SCIClose(binding->sd, 0, &err);

    free(binding);
}



int nvm_dis_rpc_enable(nvm_aq_ref ref, uint32_t adapter, nvm_dis_rpc_cb_t filter)
{
    struct binding_handle* handle;

    if (adapter >= NVM_DIS_RPC_MAX_ADAPTER)
    {
        return EINVAL;
    }

    int err = create_binding_handle(&handle, ref, adapter, filter);
    if (err != 0)
    {
        return err;
    }

    err = _nvm_rpc_handle_insert(ref, adapter, handle, (rpc_deleter_t) remove_binding_handle);
    if (err != 0)
    {
        remove_binding_handle(handle, adapter);
        return err;
    }
    
    return 0;
}



void nvm_dis_rpc_disable(nvm_aq_ref ref, uint32_t adapter)
{
    _nvm_rpc_handle_remove(ref, adapter);
}



int nvm_dis_rpc_bind(nvm_aq_ref* handle, const nvm_ctrl_t* ctrl, uint32_t adapter)
{
    nvm_aq_ref ref;
    *handle = NULL;

    const struct device* dev = _nvm_device_from_ctrl(ctrl);
    if (dev == NULL)
    {
        dprintf("Could not look up device from controller\n");
        return EINVAL;
    }

    int err = _nvm_ref_get(&ref, ctrl);
    if (err != 0)
    {
        return err;
    }

    struct binding* binding;
    err = create_binding(&binding, dev, adapter);
    if (err != 0)
    {
        _nvm_ref_put(ref);
        return err;
    }

    err = _nvm_rpc_bind(ref, binding, (rpc_deleter_t) remove_binding, (rpc_stub_t) invoke_procedure);
    if (err != 0)
    {
        remove_binding(binding);
        _nvm_ref_put(ref);
        return err;
    }

    *handle = ref;
    return 0;
}

