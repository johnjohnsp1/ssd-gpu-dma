#ifndef __NVM_INTERNAL_RPC_H__
#define __NVM_INTERNAL_RPC_H__

#include <nvm_types.h>
#include <nvm_queue.h>
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include "ctrl.h"


/* Forward declaration */
struct device;
struct nvm_admin_reference;
struct local_admin;


/*
 * Callback to delete custom instance data.
 */
typedef void (*rpc_deleter_t)(const struct nvm_admin_reference*, void*);



/*
 * RPC client-side stub definition.
 * 
 * Should perform the following actions.
 *      - marshal command
 *      - send command to remote host
 *      - wait for completion (or timeout)
 *      - unmarshal completion and return status
 */
typedef int (*rpc_stub_t)(void*, nvm_cmd_t*, nvm_cpl_t*);



/*
 * Linked list of RPC server-side binding handles.
 */
struct rpc_handle
{
    struct rpc_handle*  next;       // Pointer to next handle in list
    void*               data;       // Custom instance data
    rpc_deleter_t       release;    // Callback to release the instance data
};



/*
 * Administration queue-pair reference.
 *
 * Represents either a reference to a remote descriptor, or is a local 
 * descriptor. In other words, this handle represents both RPC clients and
 * RPC servers.
 */
struct nvm_admin_reference
{
    const nvm_ctrl_t*       ctrl;       // Controller reference
    pthread_mutex_t         lock;       // Ensure exclusive access to the reference
    struct rpc_handle*      handles;    // Linked list of binding handles (if server)
    struct rpc_server*      server;     // If not NULL, this is a local reference
    void*                   data;       // Custom instance data
    rpc_deleter_t           release;    // Callback to release instance data
    rpc_stub_t              stub;       // Client-side stub
};



/*
 * Allocate a reference wrapper and increase controller reference.
 */
int _nvm_ref_get(nvm_aq_ref* handle, const nvm_ctrl_t* ctrl);



/*
 * Free reference wrapper and decrease controller reference.
 */
void _nvm_ref_put(nvm_aq_ref ref);



/*
 * Bind reference to remote binding handle.
 */
int _nvm_rpc_bind(nvm_aq_ref ref, void* data, rpc_deleter_t deleter, rpc_stub_t stub);



/*
 * Execute a local admin command.
 */
int _nvm_local_admin(struct local_admin* aqd, const nvm_cmd_t* cmd, nvm_cpl_t* cpl);



#endif /* __NVM_INTERNAL_RPC_H__ */
