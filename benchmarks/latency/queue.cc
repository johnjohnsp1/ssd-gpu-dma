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


Queue::Queue(const Controller& ctrl, uint32_t adapter, uint32_t segmentId, uint16_t no, size_t depth)
    : no(no)
    , depth(std::min(depth, ctrl.ctrl->page_size / sizeof(nvm_cmd_t)))
{
    cq_mem = createBuffer(ctrl.ctrl, adapter, segmentId, ctrl.ctrl->page_size);
    sq_mem = createRemoteBuffer(ctrl.ctrl, adapter, no, ctrl.ctrl->page_size * (this->depth + 1));

    memset(cq_mem->vaddr, 0, cq_mem->page_size);
    int status = nvm_admin_cq_create(ctrl.aq_ref, &cq, no, cq_mem->vaddr, cq_mem->ioaddrs[0]);
    if (!nvm_ok(status))
    {
        throw error(nvm_strerror(status));
    }

    memset(sq_mem->vaddr, 0, sq_mem->page_size);
    status = nvm_admin_sq_create(ctrl.aq_ref, &sq, &cq, no, sq_mem->vaddr, sq_mem->ioaddrs[0]);
    if (!nvm_ok(status))
    {
        throw error(nvm_strerror(status));
    }
}

