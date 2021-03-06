#ifndef __LIBNVM_SAMPLES_READ_BLOCKS_OPTIONS_H__
#define __LIBNVM_SAMPLES_READ_BLOCKS_OPTIONS_H__

#include <stdint.h>
#include <stdbool.h>
#include <nvm_types.h>


struct options
{
#ifdef __DIS_CLUSTER__
    uint64_t    controller_id;
    uint32_t    adapter;
    uint32_t    segment_id; // TODO: FIXME: This needs to be memory region for sq_mem instead
#else
    const char* controller_path;
#endif
    uint32_t    namespace_id;
    size_t      num_blocks;
    size_t      offset;
    const char* output;
    bool        ascii;
};


void parse_options(int argc, char** argv, struct options* options);


#endif
