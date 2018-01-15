#include <nvm_types.h>
#include <nvm_ctrl.h>
#include <nvm_dma.h>
#include <nvm_aq.h>
#include <nvm_admin.h>
#include <nvm_util.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <getopt.h>
#include <string.h>
#include <errno.h>


static void parse_args(int argc, char** argv, uint64_t* device_id);


static void print_ctrl_info(FILE* fp, const struct nvm_ctrl_info* info)
{
    unsigned char vendor[4];
    memcpy(vendor, &info->pci_vendor, sizeof(vendor));

    char serial[21];
    memset(serial, 0, 21);
    memcpy(serial, info->serial_no, 20);

    char model[41];
    memset(model, 0, 41);
    memcpy(model, info->model_no, 40);

    char revision[9];
    memset(revision, 0, 9);
    memcpy(revision, info->firmware, 8);

    fprintf(fp, "------------- Controller information -------------\n");
    fprintf(fp, "PCI Vendor ID           : %x %x\n", vendor[0], vendor[1]);
    fprintf(fp, "PCI Subsystem Vendor ID : %x %x\n", vendor[2], vendor[3]);
    fprintf(fp, "NVM Express version     : %u.%u.%u\n",
            info->nvme_version >> 16, (info->nvme_version >> 8) & 0xff, info->nvme_version & 0xff);
    fprintf(fp, "Controller page size    : %zu\n", info->page_size);
    fprintf(fp, "Max queue entries       : %u\n", info->max_entries);
    fprintf(fp, "Serial Number           : %s\n", serial);
    fprintf(fp, "Model Number            : %s\n", model);
    fprintf(fp, "Firmware revision       : %s\n", revision);
    fprintf(fp, "Max data transfer size  : %zu\n", info->max_data_size);
    fprintf(fp, "Max outstanding commands: %zu\n", info->max_out_cmds);
    fprintf(fp, "Max number of namespaces: %zu\n", info->max_n_ns);
    fprintf(fp, "--------------------------------------------------\n");
}


static int execute_identify(const nvm_ctrl_t* ctrl, const nvm_dma_t* window, void* ptr, uint64_t ioaddr)
{
    int status;
    nvm_aq_ref ref;
    struct nvm_ctrl_info info;

    fprintf(stderr, "Resetting controller and setting up admin queues...\n");
    status = nvm_aq_create(&ref, ctrl, window);
    if (status != 0)
    {
        fprintf(stderr, "Failed to reset controller: %s\n", strerror(errno));
        return 1;
    }

    status = nvm_admin_ctrl_info(ref, &info, ptr, ioaddr);
    if (status != 0)
    {
        fprintf(stderr, "Failed to identify controller: %s\n", strerror(errno));
        status = 1;
        goto out;
    }

    print_ctrl_info(stdout, &info);

out:
    nvm_aq_destroy(ref);
    return status;
}


static int open_fd(uint64_t dev_id)
{
    int fd;
    char path[64];

    sprintf(path, "/dev/disnvme%lu", dev_id);

    fd = open(path, O_RDWR|O_NONBLOCK);
    if (fd < 0)
    {
        fprintf(stderr, "Failed to open descriptor: %s\n", strerror(errno));
        return -1;
    }

    return fd;
}


int main(int argc, char** argv)
{
    int status;
    nvm_ctrl_t* ctrl;
    nvm_dma_t* window;
    void* memory;

    long page_size = sysconf(_SC_PAGESIZE);

    uint64_t dev_id;
    parse_args(argc, argv, &dev_id);

    int fd = open_fd(dev_id);
    if (fd < 0)
    {
        exit(1);
    }

    status = nvm_ctrl_init(&ctrl, fd);
    if (status != 0)
    {
        close(fd);
        fprintf(stderr, "Failed to get controller reference: %s\n", strerror(status));
        exit(1);
    }

    close(fd);

    status = posix_memalign(&memory, ctrl->page_size, 3 * page_size);
    if (status != 0)
    {
        fprintf(stderr, "Failed to allocate page-aligned memory: %s\n", strerror(status));
        nvm_ctrl_free(ctrl);
        exit(2);
    }
    memset(memory, 0, 3 * page_size);

    status = nvm_dma_map_host(&window, ctrl, memory, 3 * page_size);
    if (status != 0)
    {
        free(memory);
        nvm_ctrl_free(ctrl);
        exit(1);
    }


    status = execute_identify(ctrl, window, NVM_DMA_OFFSET(window, 2), window->ioaddrs[2]);

    nvm_dma_unmap(window);
    free(memory);
    nvm_ctrl_free(ctrl);    

    fprintf(stderr, "Goodbye!\n");
    exit(status);
}


static void give_usage(const char* name)
{
    fprintf(stderr, "Usage: %s --ctrl=<dev id>\n", name);
}


static void show_help(const char* name)
{
    give_usage(name);
    fprintf(stderr, "    Create a manager and run an IDENTIFY CONTROLLER NVM admin command.\n\n"
            "    --ctrl     <dev id>        Device ID ('/dev/disnvmeXXX').\n"
            "    --help                     Show this information.\n");
}


static int parse_u64(const char* str, uint64_t* num, int base)
{
    char* endptr = NULL;
    uint64_t ul = strtoul(str, &endptr, base);

    if (endptr == NULL || *endptr != '\0')
    {
        return EINVAL;
    }

    *num = ul;
    return 0;
}


static void parse_args(int argc, char** argv, uint64_t* dev)
{
    static struct option opts[] = {
        { "help", no_argument, NULL, 'h' },
        { "ctrl", required_argument, NULL, 'c' },
        { NULL, 0, NULL, 0 }
    };

    int opt;
    int idx;

    bool dev_set = false;
    *dev = 0;

    while ((opt = getopt_long(argc, argv, ":hc:", opts, &idx)) != -1)
    {
        switch (opt)
        {
            case '?': // unknown option
                fprintf(stderr, "Unknown option: `%s'\n", argv[optind - 1]);
                give_usage(argv[0]);
                exit('?');

            case ':': // missing option argument
                fprintf(stderr, "Missing argument for option: `%s'\n", argv[optind - 1]);
                give_usage(argv[0]);
                exit(':');

            case 'c': // device identifier
                dev_set = true;
                if (parse_u64(optarg, dev, 10) != 0)
                {
                    give_usage(argv[0]);
                    exit('c');
                }
                break;

            case 'h':
                show_help(argv[0]);
                exit(0);
        }
    }

    if (!dev_set)
    {
        fprintf(stderr, "Device ID is not set!\n");
        give_usage(argv[0]);
        exit('c');
    }
}
