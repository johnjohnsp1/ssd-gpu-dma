#ifndef __NVM_ADMIN_H__
#define __NVM_ADMIN_H__
#ifdef __cplusplus
extern "C" {
#endif

#include <nvm_types.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>



/* List of NVM admin command opcodes */
enum nvm_admin_command_set
{
    NVM_ADMIN_DELETE_SUBMISSION_QUEUE   = (0x00 << 7) | (0x00 << 2) | 0x00,
    NVM_ADMIN_CREATE_SUBMISSION_QUEUE   = (0x00 << 7) | (0x00 << 2) | 0x01,
    NVM_ADMIN_DELETE_COMPLETION_QUEUE   = (0x00 << 7) | (0x01 << 2) | 0x00,
    NVM_ADMIN_CREATE_COMPLETION_QUEUE   = (0x00 << 7) | (0x01 << 2) | 0x01,
    NVM_ADMIN_IDENTIFY                  = (0x00 << 7) | (0x01 << 2) | 0x02,
    NVM_ADMIN_ABORT                     = (0x00 << 7) | (0x02 << 2) | 0x00,
    NVM_ADMIN_SET_FEATURES              = (0x00 << 7) | (0x02 << 2) | 0x01,
    NVM_ADMIN_GET_FEATURES              = (0x00 << 7) | (0x02 << 2) | 0x02
};



/*
 * Create IO completion queue (CQ).
 *
 * Build an NVM admin command for creating a CQ.
 */
void nvm_admin_cq_create(nvm_cmd_t* cmd, const nvm_queue_t* cq);



/* 
 * Create IO submission queue (SQ).
 *
 * Build an NVM admin command for creating an SQ. Note that the associated
 * CQ must have been created first.
 */
void nvm_admin_sq_create(nvm_cmd_t* cmd, const nvm_queue_t* cq,  const nvm_queue_t* sq);



/*
 * Delete IO submission queue (SQ).
 *
 * Build an NVM admin command for deleting an SQ.
 */
void nvm_admin_sq_delete(nvm_cmd_t* cmd, const nvm_queue_t* cq, const nvm_queue_t* sq);



/*
 * Delete IO completion queue (CQ).
 *
 * Build an NVM admin command for deleting a CQ. Note that the associated
 * SQ must have been deleted first.
 */
void nvm_admin_cq_delete(nvm_cmd_t* cmd, const nvm_queue_t* cq);



/* 
 * Identify controller.
 *
 * Build an NVM admin command for identifying the controller.
 */
void nvm_admin_identify_ctrl(nvm_cmd_t* cmd, uint64_t ioaddr);



/*
 * Identify namespace.
 */
void nvm_admin_identify_ns(nvm_cmd_t* cmd, uint32_t ns_id, uint64_t ioaddr);



/*
 * Set/get current number of queues.
 */
void nvm_admin_current_num_queues(nvm_cmd_t* cmd, bool set, uint16_t n_cqs, uint16_t n_sqs);


#ifdef __cplusplus
}
#endif
#endif /* #ifdef __NVM_ADMIN_H__ */
