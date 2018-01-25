#include "queue.h"
#include "buffer.h"
#include "ctrl.h"
#include <nvm_admin.h>
#include <nvm_types.h>
#include <nvm_util.h>
#include <nvm_error.h>
#include <stdexcept>
#include <string>
#include <cstring>
#include <memory>
#include <vector>
#include <algorithm>


using std::string;
using error = std::runtime_error;


Queue::Queue(const Controller& ctrl, uint32_t adapter, uint32_t segmentId, uint16_t no, size_t depth, bool remote)
    : no(no)
    , depth(std::min(depth, ctrl.ctrl->page_size / sizeof(nvm_cmd_t)))
{
    void* sqPtr = nullptr;
    void* cqPtr = nullptr;
    uint64_t sqAddr = 0;
    uint64_t cqAddr = 0;

    if (remote)
    {
        // Allocate submission queue and PRP lists on side closest to disk
        cq_mem = createBuffer(ctrl.ctrl, adapter, segmentId, ctrl.ctrl->page_size);
        cqPtr = cq_mem->vaddr;
        cqAddr = cq_mem->ioaddrs[0];

        sq_mem = createRemoteBuffer(ctrl.ctrl, adapter, no, ctrl.ctrl->page_size * (this->depth + 1));
        sqPtr = sq_mem->vaddr;
        sqAddr = sq_mem->ioaddrs[0];
    }
    else
    {
        // Allocate local submission queue and PRP lists
        sq_mem = createBuffer(ctrl.ctrl, adapter, segmentId, ctrl.ctrl->page_size * (this->depth + 2));
        sqPtr = sq_mem->vaddr;
        sqAddr = sq_mem->ioaddrs[0];
    
        cqPtr = NVM_DMA_OFFSET(sq_mem, 1 + this->depth);
        cqAddr = sq_mem->ioaddrs[1 + this->depth];
    }


    memset(cqPtr, 0, ctrl.ctrl->page_size);
    int status = nvm_admin_cq_create(ctrl.aq_ref, &cq, no, cqPtr, cqAddr);
    if (!nvm_ok(status))
    {
        throw error(nvm_strerror(status));
    }

    memset(sqPtr, 0, ctrl.ctrl->page_size);
    status = nvm_admin_sq_create(ctrl.aq_ref, &sq, &cq, no, sqPtr, sqAddr);
    if (!nvm_ok(status))
    {
        throw error(nvm_strerror(status));
    }
}

