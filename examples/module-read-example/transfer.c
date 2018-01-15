#define _GNU_SOURCE
#include <nvm_types.h>
#include <nvm_util.h>
#include <nvm_error.h>
#include <nvm_ctrl.h>
#include <nvm_dma.h>
#include <nvm_queue.h>
#include <nvm_cmd.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include "transfer.h"


#define MIN(a, b) ((a) <= (b) ? (a) : (b))


static int create_buffer(nvm_dma_t** buffer, const nvm_ctrl_t* ctrl, size_t size)
{
    int err;
    void* ptr = NULL;
    *buffer = NULL;

    err = posix_memalign(&ptr, ctrl->page_size, size);
    if (err != 0)
    {
        fprintf(stderr, " Failed to allocate page-aligned buffer: %s\n", strerror(err));
        return err;
    }

    err = nvm_dma_map_host(buffer, ctrl, ptr, size);
    if (err != 0)
    {
        free(ptr);
        *buffer = NULL;
        fprintf(stderr, "Failed to map buffer: %s\n", strerror(err));
        return err;
    }

    return 0;
}


static void destroy_buffer(nvm_dma_t* buffer)
{
    if (buffer != NULL)
    {
        void* ptr = buffer->vaddr;
        nvm_dma_unmap(buffer);
        free(ptr);
    }
}


struct thread_args
{
    int*            flag;
    nvm_queue_t*    cq;
    nvm_queue_t*    sq;
    size_t          cpls;
};


static void* process_completions(struct thread_args* args)
{
    nvm_cpl_t* cpl = NULL;
    nvm_queue_t* cq = args->cq;
    nvm_queue_t* sq = args->sq;
    size_t cpls = 0;

    while (*args->flag)
    {
        while ((cpl = nvm_cq_dequeue(cq)) != NULL)
        {
            nvm_sq_update(sq);

            if (!NVM_ERR_OK(cpl))
            {
                fprintf(stderr, "Command failed: %s\n", nvm_strerror(NVM_ERR_STATUS(cpl)));
            }

            ++cpls;
        }

        nvm_cq_update(cq);
        pthread_yield();
    }

    args->cpls = cpls;
    return args;
}


static int completion_thread_start(pthread_t* thread, int* flag, nvm_queue_t* cq, nvm_queue_t* sq)
{
    struct thread_args* args = malloc(sizeof(struct thread_args));
    if (args == NULL)
    {
        fprintf(stderr, "Failed to allocate CQ thread data: %s\n", strerror(errno));
        return ENOMEM;
    }

    args->flag = flag;
    args->cq = cq;
    args->sq = sq;
    args->cpls = 0;

    int err = pthread_create(thread, NULL, (void* (*)(void*)) process_completions, args);
    if (err != 0)
    {
        free(args);
        fprintf(stderr, "Failed to create CQ thread: %s\n", strerror(err));
        return err;
    }

    return 0;
}


static int completion_thread_join(pthread_t thread, uint64_t timeout, size_t expected)
{
    int err;
    struct thread_args* args = NULL;

    err = pthread_join(thread, (void**) &args);
    if (err != 0)
    {
        fprintf(stderr, "Failed to join CQ thread: %s\n", strerror(err));
        return err;
    }

    if (args == NULL)
    {
        fprintf(stderr, "This should not happen...\n");
        return 0;
    }

    while (args->cpls < expected)
    {
        nvm_cpl_t* cpl = nvm_cq_dequeue_block(args->cq, timeout);
        if (cpl == NULL)
        {
            fprintf(stderr, "Controller timed out!\n");
            return ETIME;
        }

        nvm_sq_update(args->sq);

        if (!NVM_ERR_OK(cpl))
        {
            fprintf(stderr, "Command failed: %s\n", nvm_strerror(NVM_ERR_STATUS(cpl)));
        }

        nvm_cq_update(args->cq);
        args->cpls++;
    }

    free(args);
    return 0;
}


int read_pages(const nvm_ctrl_t* ctrl, nvm_queue_t* cq, nvm_queue_t* sq, const struct transfer_info* ti)
{
    int err;
    int run = 1;
    nvm_dma_t* buffer = NULL;
    nvm_dma_t* prp_list = NULL;
    size_t cmds = 0;
    size_t nonzero = 0;
    volatile unsigned char* ptr;

    err = create_buffer(&buffer, ctrl, ti->blk_size * ti->n_blks);
    if (err != 0)
    {
        goto out;
    }
    memset(buffer->vaddr, 0xff, ti->blk_size * ti->n_blks);

    size_t chunk_size = ti->chunk_size & NVM_PAGE_MASK(ti->blk_size);

    err = create_buffer(&prp_list, ctrl, ctrl->page_size);
    if (err != 0)
    {
        goto out;
    }

    pthread_t thread;
    err = completion_thread_start(&thread, &run, cq, sq);
    if (err != 0)
    {
        goto out;
    }

    size_t remaining_blks = ti->n_blks;
    size_t buffer_page = 0;
    uint64_t current_blk = ti->start_lba;

    fprintf(stderr, "Reading from disk...\n");
    while (remaining_blks > 0)
    {
        size_t transfer_blks = MIN(remaining_blks, chunk_size / ti->blk_size);
        size_t transfer_size = transfer_blks * ti->blk_size;

        nvm_cmd_t* cmd = nvm_sq_enqueue(sq);
        if (cmd == NULL)
        {
            nvm_sq_submit(sq);
            pthread_yield();
            continue;
        }

        ++cmds;

        uint64_t dptr1 = buffer->ioaddrs[buffer_page++];
        uint64_t dptr2 = 0;

        if (transfer_size <= buffer->page_size)
        {
            dptr2 = 0;
        }
        else if (transfer_size <= 2 * buffer->page_size)
        {
            dptr2 = buffer->ioaddrs[buffer_page++];
        }
        else
        {
            dptr2 = prp_list->ioaddrs[0];
            size_t old = buffer_page;
            buffer_page += nvm_prp_list_page(transfer_size - buffer->page_size, ctrl->page_size, prp_list->vaddr, &buffer->ioaddrs[buffer_page]);
        }

        fprintf(stderr, "current_blk=%zu, n_blks=%zu, page=%zu\n", current_blk, transfer_blks, buffer_page);

        nvm_cmd_header(cmd, NVM_IO_READ, ti->ns);
        nvm_cmd_data_ptr(cmd, dptr1, dptr2);
        nvm_cmd_rw_blks(cmd, current_blk, transfer_blks);

        current_blk += transfer_blks;
        remaining_blks -= transfer_blks;
    }

    nvm_sq_submit(sq);
    pthread_yield();

    fprintf(stderr, "Waiting for completions...\n");
    run = 0;
    completion_thread_join(thread, ctrl->timeout, cmds);
    
    ptr = buffer->vaddr;
    for (size_t i = 0, n = ti->n_blks * ti->blk_size; i < n; ++i)
    {
        if (ptr[i] != 0)
        {
            nonzero += 1;
        }
    }
    fprintf(stderr, "Commands used: %zu, number of non-zero bytes: %zx\n", cmds, nonzero);

out:

    destroy_buffer(prp_list);
    destroy_buffer(buffer);
    return 0;
}


int write_zeros(const nvm_ctrl_t* ctrl, nvm_queue_t* cq, nvm_queue_t* sq, const struct transfer_info* ti)
{
    nvm_cmd_t* cmd;
    size_t remaining_blks = ti->n_blks;
    uint64_t blk_offset = ti->start_lba;
    size_t cmds = 0;
    int run = 1;
    int err;

    nvm_dma_t* buffer;
    err = create_buffer(&buffer, ctrl, ctrl->page_size);
    if (err != 0)
    {
        return err;
    }
    memset(buffer->vaddr, 0x00, ctrl->page_size);

    pthread_t thread;
    err = completion_thread_start(&thread, &run, cq, sq);
    if (err != 0)
    {
        destroy_buffer(buffer);
        return err;
    }

    fprintf(stderr, "Writing zeroes to disk...\n");
    while (remaining_blks > 0)
    {
        size_t current_blks = MIN(remaining_blks, NVM_PAGE_ALIGN(2 * buffer->page_size, ti->blk_size) / ti->blk_size);

        cmd = nvm_sq_enqueue(sq);
        if (cmd == NULL)
        {
            nvm_sq_submit(sq);
            pthread_yield();
            continue;
        }
        ++cmds;

        //nvm_cmd_header(cmd, NVM_IO_WRITE_ZEROES, ti->ns);
        //nvm_cmd_data_ptr(cmd, 0, 0);

        nvm_cmd_header(cmd, NVM_IO_WRITE, ti->ns);
        nvm_cmd_data_ptr(cmd, buffer->ioaddrs[0], buffer->ioaddrs[0]);
        nvm_cmd_rw_blks(cmd, blk_offset, current_blks);

        remaining_blks -= current_blks;
        blk_offset += current_blks;
    }

    nvm_sq_submit(sq);
    pthread_yield();

    fprintf(stderr, "Waiting for completion...\n");
    run = 0;
    completion_thread_join(thread, ctrl->timeout, cmds);

    destroy_buffer(buffer);
    return 0;
}

