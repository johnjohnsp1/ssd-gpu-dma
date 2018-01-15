#ifndef __DIS_NVM_EXAMPLES_RWEXAMPLE_H__
#define __DIS_NVM_EXAMPLES_RWEXAMPLE_H__

#include <stddef.h>
#include <stdint.h>
#include <nvm_types.h>


struct transfer_info
{
    uint32_t    ns;
    uint64_t    start_lba;
    size_t      page_size;
    size_t      blk_size;
    size_t      n_blks;
    size_t      chunk_size;
};


int write_zeros(const nvm_ctrl_t* ctrl, nvm_queue_t* cq, nvm_queue_t* sq,  const struct transfer_info* ti);


int read_pages(const nvm_ctrl_t* ctrl, nvm_queue_t* cq, nvm_queue_t* sq, const struct transfer_info* ti);


#endif
