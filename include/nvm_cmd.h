#ifndef __NVM_COMMAND_H__
#define __NVM_COMMAND_H__

#ifndef __CUDACC__
#define __device__
#define __host__
#endif

#include <nvm_types.h>
#include <stddef.h>
#include <stdint.h>



/* All namespaces identifier */
#define NVM_CMD_NS_ALL                  0xffffffff


/* List of NVM IO command opcodes */
enum nvm_io_command_set
{
    NVM_IO_FLUSH                    = (0x00 << 7) | (0x00 << 2) | 0x00, // 00h
    NVM_IO_WRITE                    = (0x00 << 7) | (0x00 << 2) | 0x01, // 01h
    NVM_IO_READ                     = (0x00 << 7) | (0x00 << 2) | 0x02, // 02h
    NVM_IO_WRITE_ZEROES             = (0x00 << 7) | (0x02 << 2) | 0x00  // 08h
};



/*
 * Set command's DWORD0 and DWORD1
 */
__device__ __host__ static inline
void nvm_cmd_header(nvm_cmd_t* cmd, uint8_t opcode, uint32_t ns_id)
{
    cmd->dword[0] &= 0xffff0000;
    cmd->dword[0] |= (0x00 << 14) | (0x00 << 8) | (opcode & 0x7f);
    cmd->dword[1] = ns_id;
}



/*
 * Set command's DPTR field (DWORD6-9)
 */
__device__ __host__ static inline
void nvm_cmd_data_ptr(nvm_cmd_t* cmd, uint64_t prp1, uint64_t prp2)
{
    cmd->dword[0] &= ~( (0x03 << 14) | (0x03 << 8) );

    cmd->dword[6] = (uint32_t) prp1;
    cmd->dword[7] = (uint32_t) (prp1 >> 32UL);
    cmd->dword[8] = (uint32_t) prp2;
    cmd->dword[9] = (uint32_t) (prp2 >> 32UL);
}



/*
 * Set command's block fields (DWORD10-12)
 */
__device__ __host__ static inline
void nvm_cmd_rw_blks(nvm_cmd_t* cmd, uint64_t start_lba, uint16_t n_blks)
{
    cmd->dword[10] = start_lba;
    cmd->dword[12] = (n_blks - 1) & 0xffff;
}



/*
 * Build a PRP list consisting of PRP entries.
 *
 * Populate a memory page with PRP entries required for a transfer.
 * Returns the number of PRP entries in the list.
 *
 * Note: currently, only a PRP lists can only be a single page
 */
__host__ __device__ static inline
size_t nvm_prp_list_page(const struct nvm_ctrl_info* info, 
                         size_t n_pages,
                         void* list_vaddr, 
                         const uint64_t* data_ioaddrs)
{
    size_t prps_per_page = info->page_size / sizeof(uint64_t);
    size_t max_prps;
    size_t i_prp;
    uint64_t* list_ptr;

    if (n_pages == 0)
    {
        return 0;
    }

    max_prps = ((info->max_data_size + info->page_size - 1) & ~(info->page_size - 1)) / info->page_size;

    if (max_prps < n_pages)
    {
        n_pages = max_prps;
    }

    if (prps_per_page < n_pages)
    {
        n_pages = prps_per_page;
    }
   
    list_ptr = (uint64_t*) list_vaddr;
    for (i_prp = 0; i_prp < n_pages; ++i_prp)
    {
        list_ptr[i_prp] = data_ioaddrs[i_prp];
    }

    return i_prp * info->page_size;
}


#ifndef __CUDACC__
#undef __device__
#undef __host__
#endif

#endif /* __NVM_COMMAND_H__ */