#ifndef __TRANSFER_H__
#define __TRANSFER_H__

#include "ctrl.h"
#include <vector>
#include <cstddef>
#include <cstdint>
#include <vector>


struct Transfer
{
    bool        write;
    size_t      startBlock;
    size_t      numBlocks;
    size_t      pages;
};


typedef std::vector<Transfer> TransferList;


#endif
