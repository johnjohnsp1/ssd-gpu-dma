#include <nvm_types.h>
#include <nvm_ctrl.h>
#include <nvm_dma.h>
#include <nvm_aq.h>
#include <nvm_admin.h>
#include <nvm_error.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <getopt.h>
#include <string.h>
#include <errno.h>
#include <sisci_api.h>
#include "integrity.h"


#define MAX_ADAPTERS NVM_DIS_RPC_MAX_ADAPTER

struct arguments
{
    uint64_t        device_id;
    uint32_t        adapter;
    uint32_t        segment_id;
    uint32_t        ns_id;
    uint16_t        n_queues;
    size_t          read_bytes;
    const char*     filename;
};



static bool parse_number(uint64_t* number, const char* str, int base, uint64_t lo, uint64_t hi)
{
    char* endptr = NULL;
    uint64_t ul = strtoul(str, &endptr, base);

    if (endptr == NULL || *endptr != '\0')
    {
        return false;
    }

    if (lo < hi && (ul < lo || ul >= hi))
    {
        return false;
    }

    *number = ul;
    return true;
}



static void parse_arguments(int argc, char** argv, struct arguments* args)
{
    struct option opts[] = {
        { "help", no_argument, NULL, 'h' },
        { "read", required_argument, NULL, 'r' },
        { "ctrl", required_argument, NULL, 'c' },
        { "namespace", required_argument, NULL, 'n' },
        { "adapter", required_argument, NULL, 'a' },
        { "id-offset" , required_argument, NULL, 1 },
        { "queues", required_argument, NULL, 'q' },
        { NULL, 0, NULL, 0 }
    };

    int opt;
    int idx;
    uint64_t num;

    args->device_id = 0;
    args->adapter = 0;
    args->segment_id = 5;
    args->ns_id = 1;
    args->n_queues = 1;
    args->read_bytes = 0;
    args->filename = NULL;

    while ((opt = getopt_long(argc, argv, ":hr:c:n:a:q:", opts, &idx)) != -1)
    {
        switch (opt)
        {
            case '?': // unknown option
                fprintf(stderr, "Unknown option: `%s'\n", argv[optind - 1]);
                exit(4);

            case ':': // missing option argument
                fprintf(stderr, "Missing argument for option `%s'\n", argv[optind - 1]);
                exit(4);

            case 'h':
                fprintf(stderr, "Usage: %s --ctrl=device-id [--read=bytes] [-a adapter] [-n namespace] [-q queues] filename\n", argv[0]);
                exit(4);

            case 'r':
                if ( !parse_number(&args->read_bytes, optarg, 0, 0, 0) || args->read_bytes == 0 )
                {
                    fprintf(stderr, "Invalid number of bytes: `%s'\n", optarg);
                    exit(1);
                }
                break;

            case 'c': // device identifier
                if (! parse_number(&args->device_id, optarg, 0, 0, 0) )
                {
                    fprintf(stderr, "Invalid controller identifier: `%s'\n", optarg);
                    exit(1);
                }
                break;

            case 'n': // specify namespace number
                if (! parse_number(&num, optarg, 0, 0, 0) )
                {
                    fprintf(stderr, "Invalid controller identifier: `%s'\n", optarg);
                    exit(3);
                }
                args->ns_id = (uint32_t) num;
                break;

            case 1: // set segment identifier offset
                if (! parse_number(&num, optarg, 0, 0, 0) )
                {
                    fprintf(stderr, "Invalid offset: `%s'\n", optarg);
                    exit(1);
                }
                args->segment_id = (uint32_t) num;
                break;

            case 'a': // set adapter number
                if (! parse_number(&num, optarg, 10, 0, MAX_ADAPTERS) )
                {
                    fprintf(stderr, "Invalid adapter number: `%s'\n", optarg);
                    exit(1);
                }
                args->adapter = (uint32_t) num;
                break;

            case 'q': // set number of queues
                if (! parse_number(&num, optarg, 0, 1, 0xffff) )
                {
                    fprintf(stderr, "Invalid number of queues: `%s'\n", optarg);
                    exit(3);
                }
                args->n_queues = (uint16_t) num;
                break;
        }
    }

    argc -= optind;
    argv += optind;

    if (args->device_id == 0)
    {
        fprintf(stderr, "No controller specified!\n");
        exit(1);
    }

    if (argc < 1)
    {
        fprintf(stderr, "File not specified!\n");
        exit(2);
    }
    else if (argc > 1)
    {
        fprintf(stderr, "More than one filename specified!\n");
        exit(2);
    }

    args->filename = argv[0];
}



static int identify_controller(nvm_aq_ref ref, const struct arguments* args, struct disk* disk)
{
    int status;
    nvm_dma_t* window;
    struct nvm_ctrl_info info;

    const nvm_ctrl_t* ctrl = nvm_ctrl_from_aq_ref(ref);

    status = nvm_dis_dma_create(&window, ctrl, args->adapter, args->segment_id, ctrl->page_size);
    if (!nvm_ok(status))
    {
        fprintf(stderr, "Failed to create buffer: %s\n", nvm_strerror(status));
        return status;
    }

    status = nvm_admin_ctrl_info(ref, &info, window->vaddr, window->ioaddrs[0]);
    if (!nvm_ok(status))
    {
        fprintf(stderr, "Failed to identify controller: %s\n", nvm_strerror(status));
    }

    disk->page_size = info.page_size;
    disk->max_data_size = info.max_data_size;

    nvm_dma_unmap(window);
    return status;
}



static int identify_namespace(nvm_aq_ref ref, const struct arguments* args, struct disk* disk)
{
    int status;
    nvm_dma_t* window;
    struct nvm_ns_info info;

    const nvm_ctrl_t* ctrl = nvm_ctrl_from_aq_ref(ref);

    status = nvm_dis_dma_create(&window, ctrl, args->adapter, args->segment_id, ctrl->page_size);
    if (!nvm_ok(status))
    {
        fprintf(stderr, "Failed to create buffer: %s\n", nvm_strerror(status));
        return status;
    }

    status = nvm_admin_ns_info(ref, &info, args->ns_id, window->vaddr, window->ioaddrs[0]);
    if (!nvm_ok(status))
    {
        fprintf(stderr, "Failed to identify namespace: %s\n", nvm_strerror(status));
    }

    disk->ns_id = info.ns_id;
    disk->block_size = info.lba_data_size;

    nvm_dma_unmap(window);
    return status;
}


static void remove_queues(struct queue* queues, uint16_t n_queues)
{
    uint16_t i;

    if (queues != NULL)
    {
        for (i = 0; i < n_queues + 1; ++i)
        {
            remove_queue(&queues[i]);
        }

        free(queues);
    }
}



static int request_queues(nvm_aq_ref ref, struct arguments* args, struct queue** queues)
{
    struct queue* q;
    *queues = NULL;
    uint16_t i;

    uint16_t n_cqs = 1;
    uint16_t n_sqs = args->n_queues;

    int status = nvm_admin_request_num_queues(ref, &n_cqs, &n_sqs);

    if (!nvm_ok(status))
    {
        fprintf(stderr, "Failed to request queues: %s\n", nvm_strerror(status));
        return status;
    }

    args->n_queues = n_sqs < args->n_queues ? n_sqs : args->n_queues;

    // Allocate queue descriptors
    q = calloc(args->n_queues + 1, sizeof(struct queue));
    if (q == NULL)
    {
        fprintf(stderr, "Failed to allocate queues: %s\n", strerror(errno));
        return ENOMEM;
    }

    // Create completion queue
    status = create_queue(&q[0], ref, NULL, 1, args->adapter, args->segment_id++);
    if (status != 0)
    {
        free(q);
        return status;
    }

    // Create submission queues
    for (i = 0; i < args->n_queues; ++i)
    {
        status = create_queue(&q[i + 1], ref, &q[0], i+1, args->adapter, args->segment_id++);
        if (status != 0)
        {
            remove_queues(q, i);
            return status;
        }
    }
    
    *queues = q;
    return status;
}



int main(int argc, char** argv)
{
    sci_error_t err;
    struct arguments args;
    nvm_ctrl_t* ctrl;
    int status;
    nvm_dma_t* aq_dma;
    nvm_aq_ref aq_ref;
    struct disk disk;
    struct queue* queues = NULL;
    struct buffer buffer;
    
    // Parse command line arguments
    parse_arguments(argc, argv, &args);

    // Open file descriptor and find file size
    FILE* fp = fopen(args.filename, args.read_bytes ? "w" : "r");
    if (fp == NULL)
    {
        fprintf(stderr, "Failed to open file `%s': %s\n", 
                args.filename, strerror(errno));
        exit(2);
    }

    // Calculate bytes
    off_t file_size = 0;
    if (args.read_bytes > 0)
    {
        file_size = args.read_bytes;
    }
    else
    {
        fseek(fp, 0, SEEK_END);
        file_size = ftell(fp);
        rewind(fp);

        if (file_size == 0)
        {
            fprintf(stderr, "File `%s' is empty!\n", args.filename);
            fclose(fp);
            exit(2);
        }
    }

    fprintf(stderr, "Using file `%s' (%lu bytes)\n", args.filename, file_size);

    // Start SISCI API
    SCIInitialize(0, &err);

    // Get controller reference
    status = nvm_dis_ctrl_init(&ctrl, args.device_id, args.adapter);
    if (status != 0)
    {
        fclose(fp);
        fprintf(stderr, "Failed to get controller reference: %s\n", strerror(status));
        exit(1);
    }

    // Create admin queues
    status = nvm_dis_dma_create(&aq_dma, ctrl, args.adapter, args.segment_id++, ctrl->page_size * 2);
    if (status != 0)
    {
        nvm_ctrl_free(ctrl);
        fclose(fp);
        fprintf(stderr, "Failed to create admin queue pair: %s\n", strerror(status));
        exit(1);
    }

    fprintf(stderr, "Resetting controller and configuring admin queue pair...\n");
    status = nvm_aq_create(&aq_ref, ctrl, aq_dma);
    if (status != 0)
    {
        nvm_dma_unmap(aq_dma);
        nvm_ctrl_free(ctrl);
        fclose(fp);
        fprintf(stderr, "Failed to create admin queue pair: %s\n", strerror(status));
        exit(1);
    }

    // Allocate buffer
    status = create_buffer(&buffer, aq_ref, NVM_CTRL_ALIGN(ctrl, file_size), args.adapter, args.segment_id++);
    if (status != 0)
    {
        nvm_aq_destroy(aq_ref);
        nvm_dma_unmap(aq_dma);
        nvm_ctrl_free(ctrl);
        fclose(fp);
        exit(1);
    }

    // Extract controller and namespace information
    status = identify_controller(aq_ref, &args, &disk);
    if (status != 0)
    {
        goto out;
    }

    status = identify_namespace(aq_ref, &args, &disk);
    if (status != 0)
    {
        goto out;
    }

    if (args.n_queues > NVM_PAGE_ALIGN(file_size, disk.block_size) / disk.block_size)
    {
        args.n_queues = NVM_PAGE_ALIGN(file_size, disk.block_size) / disk.block_size;
    }

    // Create queues
    status = request_queues(aq_ref, &args, &queues);
    if (status != 0)
    {
        goto out;
    }

    fprintf(stderr, "Using %u submission queues:\n", args.n_queues);

    if (args.read_bytes > 0)
    {
//        status = disk_read(&desc, ref, args.n_queues, fp, file_size);
    }
    else
    {
        status = disk_write(&disk, &buffer, queues, args.n_queues, fp, file_size);
    }

out:
    remove_queues(queues, args.n_queues);
    nvm_aq_destroy(aq_ref);
    nvm_dma_unmap(aq_dma);
    remove_buffer(&buffer);
    nvm_ctrl_free(ctrl);
    fclose(fp);
    SCITerminate();
    exit(status);
}
