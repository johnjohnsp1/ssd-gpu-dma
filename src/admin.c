#include <nvm_types.h>
#include <nvm_admin.h>
#include <nvm_cmd.h>
#include <nvm_rpc.h>
#include <nvm_util.h>
#include <nvm_error.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include "rpc.h"
#include "regs.h"
#include "dprintf.h"



void nvm_admin_cq_create(nvm_cmd_t* cmd, const nvm_queue_t* cq)
{
    nvm_cmd_header(cmd, NVM_ADMIN_CREATE_COMPLETION_QUEUE, 0);
    nvm_cmd_data_ptr(cmd, cq->ioaddr, 0);

    cmd->dword[10] = (((uint32_t) cq->max_entries - 1) << 16) | cq->no;
    cmd->dword[11] = (0x0000 << 16) | (0x00 << 1) | 0x01;
}



void nvm_admin_sq_create(nvm_cmd_t* cmd, const nvm_queue_t* sq, const nvm_queue_t* cq)
{
    nvm_cmd_header(cmd, NVM_ADMIN_CREATE_SUBMISSION_QUEUE, 0);
    nvm_cmd_data_ptr(cmd, sq->ioaddr, 0);

    cmd->dword[10] = (((uint32_t) sq->max_entries - 1) << 16) | sq->no;
    cmd->dword[11] = (((uint32_t) cq->no) << 16) | (0x00 << 1) | 0x01;
}



void nvm_admin_current_num_queues(nvm_cmd_t* cmd, bool set, uint16_t n_cqs, uint16_t n_sqs)
{
    nvm_cmd_header(cmd, set ? NVM_ADMIN_SET_FEATURES : NVM_ADMIN_GET_FEATURES, 0);
    nvm_cmd_data_ptr(cmd, 0, 0);

    cmd->dword[10] = (0x00 << 8) | 0x07;
    cmd->dword[11] = set ? ((n_cqs - 1) << 16) | (n_sqs - 1) : 0;
}



void nvm_admin_identify_ctrl(nvm_cmd_t* cmd, uint64_t ioaddr)
{
    nvm_cmd_header(cmd, NVM_ADMIN_IDENTIFY, 0);
    nvm_cmd_data_ptr(cmd, ioaddr, 0);

    cmd->dword[10] = (0 << 16) | 0x01;
    cmd->dword[11] = 0;
}



void nvm_admin_identify_ns(nvm_cmd_t* cmd, uint32_t ns_id, uint64_t ioaddr)
{
    nvm_cmd_header(cmd, NVM_ADMIN_IDENTIFY, ns_id);
    nvm_cmd_data_ptr(cmd, ioaddr, 0);

    cmd->dword[10] = (0 << 16) | 0x00;
    cmd->dword[11] = 0;
}



int nvm_rpc_ctrl_info(nvm_aq_ref ref, struct nvm_ctrl_info* info, void* ptr, uint64_t ioaddr)
{
    if (info == NULL)
    {
        return EINVAL;
    }

    memset(info, 0, sizeof(struct nvm_ctrl_info));
    const nvm_ctrl_t* ctrl = ref->ctrl;

    info->nvme_version = (uint32_t) *VER(ref->ctrl->mm_ptr);
    info->page_size = ctrl->page_size;
    info->db_stride = 1UL << ctrl->dstrd;
    info->timeout = ctrl->timeout;
    info->contiguous = !!CAP$CQR(ctrl->mm_ptr);
    info->max_entries = ctrl->max_entries;

    if (ptr == NULL || ioaddr == 0)
    {
        return 0;
    }

    memset(ptr, 0, 0x1000);

    nvm_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    nvm_admin_identify_ctrl(&cmd, ioaddr);

    nvm_cpl_t cpl;
    memset(&cpl, 0, sizeof(cpl));

    int err = nvm_raw_rpc(ref, &cmd, &cpl);
    
    if (!nvm_ok(err))
    {
        dprintf("Identify controller failed: %s\n", nvm_strerror(err));
        return err;
    }

    const unsigned char* bytes = ((const unsigned char*) ptr);
    memcpy(info->pci_vendor, bytes, 4);
    memcpy(info->serial_no, bytes + 4, 20);
    memcpy(info->model_no, bytes + 24, 40);
    memcpy(info->firmware, bytes + 64, 8);

    info->max_data_size = (1UL << bytes[77]) * (1UL << (12 + CAP$MPSMIN(ctrl->mm_ptr)));
    info->sq_entry_size = 1 << _RB(bytes[512], 3, 0);
    info->cq_entry_size = 1 << _RB(bytes[513], 3, 0);
    info->max_out_cmds = *((uint16_t*) (bytes + 514));
    info->max_n_ns = *((uint32_t*) (bytes + 516));

    return 0;
}

