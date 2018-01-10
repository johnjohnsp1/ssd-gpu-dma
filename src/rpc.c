#ifdef _SISCI
#include <sisci_api.h>
#include <sisci_types.h>
#include <sisci_error.h>
#ifndef __DIS_CLUSTER__
#define __DIS_CLUSTER__
#endif
#endif

#include <nvm_types.h>
#include <nvm_aq.h>
#include <nvm_rpc.h>
#include <nvm_queue.h>
#include <nvm_ctrl.h>
#include <nvm_cmd.h>
#include <nvm_util.h>
#include <nvm_error.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include "dis_device.h"
#include "rpc.h"
#include "ctrl.h"
#include "util.h"
#include "dprintf.h"



/*
 * Local admin queue-pair descriptor.
 */
struct local_admin
{
    nvm_queue_t         acq;        // Admin completion queue (ACQ)
    nvm_queue_t         asq;        // Admin submission queue (ASQ)
    uint64_t            timeout;    // Controller timeout
};



/*
 * Helper function to remove all server handles.
 */
static void release_handles(nvm_aq_ref ref)
{
    struct rpc_handle* curr;
    struct rpc_handle* next;

    if (ref != NULL)
    {
        curr = NULL;
        next = ref->handles;
        while (next != NULL)
        {
            curr = next;
            next = curr->next;

            if (curr->release != NULL)
            {
                curr->release(ref, curr->data);
            }

            free(curr);
        }

        ref->handles = NULL;
    }
}


/* 
 * Helper function to allocate an admin reference.
 */
int _nvm_ref_get(nvm_aq_ref* handle, const nvm_ctrl_t* ctrl)
{
    int err;

    *handle = NULL;

    nvm_aq_ref ref = (nvm_aq_ref) malloc(sizeof(struct nvm_admin_reference));
    if (ref == NULL)
    {
        dprintf("Failed to allocate reference: %s\n", strerror(errno));
        return ENOMEM;
    }

    err = pthread_mutex_init(&ref->lock, NULL);
    if (err != 0)
    {
        dprintf("Failed to initialize reference lock: %s\n", strerror(err));
        goto free_handle;
    }

    //ref->device = NULL;
    ref->ctrl = ctrl;
    ref->handles = NULL;
    ref->data = NULL;
    ref->release = NULL;
    ref->stub = NULL;

//#ifdef _SISCI
//    if (_nvm_device_from_ctrl(ctrl) != NULL)
//    {
//        err = _nvm_device_dup(&ref->device, _nvm_device_from_ctrl(ctrl));
//        if (err != 0)
//        {
//            dprintf("Failed to increase device reference: %s\n", strerror(err));
//            goto destroy_mtx;
//        }
//    }
//#endif

    *handle = ref;
    return 0;


destroy_mtx:
    pthread_mutex_destroy(&ref->lock);

free_handle:
    free(ref);

    return err;
}



/* 
 * Helper function to free an admin reference.
 */
void _nvm_ref_put(nvm_aq_ref ref)
{
    if (ref != NULL)
    {
        pthread_mutex_lock(&ref->lock);

        release_handles(ref);

        if (ref->release != NULL)
        {
            ref->release(ref, ref->data);
        }

//#ifdef _SISCI
//        if (ref->device != NULL)
//        {
//            _nvm_device_put(ref->device);
//        }
//#endif

        pthread_mutex_unlock(&ref->lock);

        pthread_mutex_destroy(&ref->lock);
        free(ref);
    }
}



/* 
 * Execute an NVM admin command.
 */
int _nvm_local_admin(struct local_admin* admin, const nvm_cmd_t* cmd, nvm_cpl_t* cpl)
{
    nvm_cmd_t* in_queue_cmd;
    nvm_cpl_t* in_queue_cpl;

    // Try to enqueue a message
    if ((in_queue_cmd = nvm_sq_enqueue(&admin->asq)) == NULL)
    {
        // Queue was full, but we're holding the lock so no blocking
        return EAGAIN;
    }
    
    // Copy command into queue slot (but keep original id)
    uint16_t in_queue_id = *NVM_CMD_CID(in_queue_cmd);
    memcpy(in_queue_cmd, cmd, sizeof(nvm_cmd_t));
    *NVM_CMD_CID(in_queue_cmd) = in_queue_id;

    // Submit command and wait for completion
    nvm_sq_submit(&admin->asq);

    in_queue_cpl = nvm_cq_dequeue_block(&admin->acq, admin->timeout);
    if (in_queue_cpl == NULL)
    {
        dprintf("Waiting for admin queue completion timed out\n");
        return ETIME;
    }

    nvm_sq_update(&admin->asq);

    // Copy completion and return
    memcpy(cpl, in_queue_cpl, sizeof(nvm_cpl_t));
    *NVM_CPL_CID(cpl) = *NVM_CMD_CID(cmd);

    return 0;
}



/*
 * Helper function to create a local admin descriptor.
 */
static struct local_admin* create_admin(const nvm_ctrl_t* ctrl, const nvm_dma_t* window)
{
    struct local_admin* admin = (struct local_admin*) malloc(sizeof(struct local_admin));
    
    if (admin == NULL)
    {
        dprintf("Failed to create admin queue-pair descriptors: %s\n", strerror(errno));
        return NULL;
    }

    nvm_queue_clear(&admin->acq, ctrl, true, 0, window->vaddr, window->ioaddrs[0]);

    void* asq_vaddr = (void*) (((unsigned char*) window->vaddr) + window->page_size);
    nvm_queue_clear(&admin->asq, ctrl, false, 0, asq_vaddr, window->ioaddrs[1]);

    memset(window->vaddr, 0, 2 * window->page_size);

    admin->timeout = ctrl->timeout;

    return admin;
}


/*
 * Helper function to remove an admin descriptor.
 */
static void remove_admin(const struct nvm_admin_reference* ref, struct local_admin* admin)
{
    if (ref != NULL)
    {
        free(admin);
    }
}



int nvm_raw_rpc(nvm_aq_ref ref, nvm_cmd_t* cmd, nvm_cpl_t* cpl)
{
    int err;

    err = pthread_mutex_lock(&ref->lock);
    if (err != 0)
    {
        dprintf("Failed to take reference lock\n");
        return NVM_ERR_PACK(NULL, EBADF);
    }

    err = ref->stub(ref->data, cmd, cpl);

    pthread_mutex_unlock(&ref->lock);

    return NVM_ERR_PACK(cpl, err);
}



int nvm_rpc_bind(nvm_aq_ref ref, void* data, rpc_deleter_t release, rpc_stub_t stub)
{
    int err;

    err = pthread_mutex_lock(&ref->lock);
    if (err != 0)
    {
        dprintf("Failed to take reference lock\n");
        return EBADF;
    }

    if (ref->data != NULL || ref->stub != NULL)
    {
        pthread_mutex_unlock(&ref->lock);
        return EINVAL;
    }

    ref->data = data;
    ref->release = release;
    ref->stub = stub;

    return 0;
}



/*
 * Create admin queues locally.
 */
int nvm_aq_create(nvm_aq_ref* handle, const nvm_ctrl_t* ctrl, const nvm_dma_t* window)
{
    int err;
    nvm_aq_ref ref;

    *handle = NULL;

    if (ctrl->page_size != window->page_size)
    {
        dprintf("Controller page size differs from DMA window page size\n");
        return EINVAL;
    }
    else if (window->n_ioaddrs < 2)
    {
        dprintf("DMA window is not large enough\n");
        return EINVAL;
    }
    else if (window->vaddr == NULL)
    {
        dprintf("DMA window is not mapped into virtual address space\n");
        return EINVAL;
    }

    // Allocate reference
    err = _nvm_ref_get(&ref, ctrl);
    if (err != 0)
    {
        return err;
    }

    // Allocate admin descriptor
    ref->data = create_admin(ref->ctrl, window);
    if (ref->data == NULL)
    {
        _nvm_ref_put(ref);
        return ENOMEM;
    }

    ref->stub = (rpc_stub_t) _nvm_local_admin;
    ref->release = (rpc_deleter_t) remove_admin;

    // Reset controller
    const struct local_admin* admin = (const struct local_admin*) ref->data;
    nvm_raw_ctrl_reset(ctrl, admin->acq.ioaddr, admin->asq.ioaddr);
    
    *handle = ref;
    return 0;
}



void nvm_aq_destroy(nvm_aq_ref ref)
{
    _nvm_ref_put(ref);
}


