#include <nvm_dma.h>
#include <nvm_types.h>
#include <nvm_aq.h>
#include <nvm_admin.h>
#include <nvm_ctrl.h>
#include <nvm_util.h>
#include <sisci_api.h>
#include <stdio.h>
#include <string.h>
#include "util.h"


int main(int argc, char** argv)
{
    sci_error_t err;
    int status;
    nvm_ctrl_t* ctrl;
    nvm_dma_t* dma;
    nvm_aq_ref rpc;

    uint64_t dev_id = 0x80000;
    
    SCIInitialize(0, &err);

    status = nvm_dis_ctrl_init(&ctrl, dev_id, 0);

    status = nvm_dis_dma_create(&dma, ctrl, 0, 0, 3 * 0x1000);
    if (status != 0)
    {
        fprintf(stderr, "%s\n", strerror(status));
    }

    fprintf(stderr, "%zu\n", dma->n_ioaddrs);

    status = nvm_aq_create(&rpc, ctrl, dma);
    if (status != 0)
    {
        fprintf(stderr, "%s\n", strerror(status));
    }

    struct nvm_ctrl_info info;
    status = nvm_admin_ctrl_info(rpc, &info, NVM_DMA_OFFSET(dma, 2), dma->ioaddrs[2]);

    print_ctrl_info(stdout, &info);

    nvm_dma_unmap(dma);
    nvm_ctrl_free(ctrl);

    SCITerminate();
    return 0;
}
