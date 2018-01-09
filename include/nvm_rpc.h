#ifndef __NVM_RPC_H__
#define __NVM_RPC_H__
#ifdef __cplusplus
extern "C" {
#endif

#include <nvm_types.h>
#include <stdint.h>


//int nvm_tcp_rpc_bind(nvm_aq_ref* ref, const char* hostname, uint16_t port);



#ifdef __DIS_CLUSTER__

/*
 * Bind admin queue-pair reference to remote handle.
 */
int nvm_dis_rpc_bind(nvm_aq_ref* ref, const nvm_ctrl_t* ctrl);



/*
 * Unbind admin queue-pair reference from remote handle.
 * After t
 */
void nvm_dis_rpc_unbind(nvm_aq_ref ref);

#endif


/*
 * Relay NVM admin command.
 *
 * Use a local AQ pair reference to relay a NVM admin command to ASQ and get
 * a corresponding completion from the ACQ. This function will block until 
 * either a timeout occurs or until the command is completed.
 *
 * For this function and all RPC functions:
 * - If return value is zero, it indicates success.
 * - If return value is positive, it indicates an errno.
 * - If return value is negative, it indicates an NVM error.
 *
 * Use the error handling macros in nvm_error.h
 *
 * Note: The command can be modified.
 */
int nvm_raw_rpc(nvm_aq_ref ref, nvm_cmd_t* cmd, nvm_cpl_t* cpl);



/*
 * Get controller information.
 */
int nvm_rpc_ctrl_info(nvm_aq_ref ref,               // AQ pair reference
                      struct nvm_ctrl_info* info,   // Controller information structure
                      void* buffer,                 // Temporary buffer (must be at least 4 KB)
                      uint64_t ioaddr);             // Bus address of buffer as seen by the controller



/* 
 * Get namespace information.
 */
int nvm_rpc_ns_info(nvm_aq_ref ref,                 // AQ pair reference
                    struct nvm_ns_info* info,       // NVM namespace information
                    uint32_t ns_id,                 // Namespace identifier
                    void* buffer,                   // Temporary buffer (must be at least 4 KB)
                    uint64_t ioaddr);               // Bus address of buffer as seen by controller
        


/*
 * Make controller allocate and reserve queues.
 */
int nvm_rpc_set_num_queues(nvm_aq_ref ref, uint16_t n_cqs, uint16_t n_sqs);


/*
 * Retrieve the number of allocated queues.
 */
int nvm_rpc_get_num_queues(nvm_aq_ref ref, uint16_t* n_cqs, uint16_t* n_sqs);


/*
 * Make controller allocate number of queues before issuing them.
 */
int nvm_rpc_request_num_queues(nvm_aq_ref ref, uint16_t* n_cqs, uint16_t* n_sqs);


/*
 * Create IO completion queue (CQ)
 */
int nvm_rpc_cq_create(nvm_aq_ref ref,               // AQ pair reference
                      nvm_queue_t* cq,              // CQ descriptor
                      uint16_t id,                  // Queue identifier
                      void* qmem,                   // Queue memory (virtual memory)
                      uint64_t ioaddr);             // Bus address to queue memory as seen by controller


/*
 * Create IO submission queue (SQ)
 */
int nvm_rpc_sq_create(nvm_aq_ref ref,               // AQ pair reference
                      nvm_queue_t* sq,              // SQ descriptor
                      const nvm_queue_t* cq,        // Descriptor to paired CQ
                      uint16_t id,                  // Queue identifier
                      void* qmem,                   // Queue memory (virtual)
                      uint64_t ioaddr);             // Bus address to queue as seen by controller

#ifdef __cplusplus
}
#endif
#endif /* #ifdef __NVM_RPC_H__ */
