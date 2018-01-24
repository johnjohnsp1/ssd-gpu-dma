#include <cuda.h>
#include "settings.h"
#include <nvm_types.h>
#include <nvm_cmd.h>
#include <nvm_aq.h>
#include <sstream>
#include <string>
#include <cstdint>
#include <getopt.h>

using std::string;

static const struct option options[] = {
    { .name = "help", .has_arg = no_argument, .flag = nullptr, .val = 'h' },
    { .name = "ctrl", .has_arg = required_argument, .flag = nullptr, .val = 'c' },
    { .name = "cuda-device", .has_arg = required_argument, .flag = nullptr, .val = 'g' },
    { .name = "gpu", .has_arg = required_argument, .flag = nullptr, .val = 'g' },
    { .name = "namespace", .has_arg = required_argument, .flag = nullptr, .val = 'i' },
    { .name = "adapter", .has_arg = required_argument, .flag = nullptr, .val = 'a' },
    { .name = "num-blocks", .has_arg = required_argument, .flag = nullptr, .val = 'n' },
    { .name = "blocks", .has_arg = required_argument, .flag = nullptr, .val = 'n' },
    { .name = "offset", .has_arg = required_argument, .flag  = nullptr, .val = 'o' },
    { .name = "queues", .has_arg = required_argument, .flag = nullptr, .val = 'q' },
    { .name = "depth", .has_arg = required_argument, .flag = nullptr, .val = 'd' },
    { .name = "warmups", .has_arg = required_argument, .flag = nullptr, .val = 'w' },
    { .name = "repetitions", .has_arg = required_argument, .flag = nullptr, .val = 'r' },
    { .name = "repeat", .has_arg = no_argument, .flag = nullptr, .val = AccessPattern::REPEAT },
    { .name = "sequential", .has_arg = no_argument, .flag = nullptr, .val =  AccessPattern::SEQUENTIAL },
    { .name = "random", .has_arg = no_argument, .flag = nullptr, .val = AccessPattern::RANDOM },
    { .name = "verify", .has_arg = required_argument, .flag = nullptr, .val = 'v' },
    { .name = nullptr, .has_arg = no_argument, .flag = nullptr, .val = 0 }
};



static string usageString(const char* name)
{
    return name + string(": [-a adapter] --ctrl id [--gpu-no] --num-blocks block-count [-q queue-count] [--depth queue-depth]");
}



static string helpString(const char* name)
{
    string usage(usageString(name));
    return usage;
}


static int maxCudaDevice()
{
    int deviceCount = 0;
    cudaError_t err = cudaGetDeviceCount(&deviceCount);
    if (err != cudaSuccess)
    {
        throw string("Unexpected error: ") + cudaGetErrorString(err);
    }
    return deviceCount;
}



Settings::Settings()
{
    cudaDevice = -1;
    controllerId = 0;
    adapter = 0;
    segmentId = 0;
    nvmNamespace = 1;
    warmups = 10;
    repetitions = 1000;
    numQueues = 1;
    queueDepth = 32;
    numBlocks = 0;
    startBlock = 0;
    pattern = SEQUENTIAL;
    filename = nullptr;
}


static uint64_t parseNumber(const char* str, int base)
{
    char* end = nullptr;
    uint64_t n = strtoul(str, &end, base);

    if (end == nullptr || *end != '\0')
    {
        throw string("Invalid number: `") + str + string("'");
    }

    return n;
}


static uint64_t parseNumber(const char* str)
{
    return parseNumber(str, 0);
}



void Settings::parseArguments(int argc, char** argv)
{
    int index;
    int option;

    while ((option = getopt_long(argc, argv, ":hc:g:i:a:n:o:q:d:w:r:v:", options, &index)) != -1)
    {
        switch (option)
        {
            case '?':
                throw string("Unknown option: `") + argv[optind - 1] + string("'");

            case ':':
                throw string("Missing argument for option ") + argv[optind - 1];

            case 'h':
                throw helpString(argv[0]);

            case AccessPattern::REPEAT:
            case AccessPattern::SEQUENTIAL:
            case AccessPattern::RANDOM:
                pattern = AccessPattern(option);
                break;

            case 'c':
                controllerId = (uint64_t) parseNumber(optarg);
                break;

            case 'g':
                cudaDevice = (int) parseNumber(optarg, 10);
                if (cudaDevice < 0 || cudaDevice >= maxCudaDevice())
                {
                    throw string("Invalid CUDA device: ") + optarg;
                }
                break;

            case 'i':
                nvmNamespace = (uint32_t) parseNumber(optarg);
                if (nvmNamespace == NVM_CMD_NS_ALL || nvmNamespace == 0)
                {
                    throw string("Not a valid NVM namespace: ") + optarg;
                }
                break;

            case 'a':
                adapter = (uint32_t) parseNumber(optarg, 10);
                if (adapter >= NVM_DIS_RPC_MAX_ADAPTER)
                {
                    throw string("Invalid adapter number: ") + optarg;
                }
                break;

            case 'n':
                numBlocks = (size_t) parseNumber(optarg);
                if (numBlocks == 0)
                {
                    throw string("Number of blocks must be at least 1");
                }
                break;

            case 'o':
                startBlock = (size_t) parseNumber(optarg);
                break;

            case 'q':
                numQueues = (size_t) parseNumber(optarg);
                if (numQueues == 0 || numQueues > 0xffff)
                {
                    throw string("Invalid number of IO queues specified, must be in range 1-65535");
                }
                break;

            case 'd':
                queueDepth = (size_t) parseNumber(optarg);
                if (queueDepth < 1 || queueDepth >= 64)
                {
                    throw string("Invalid queue depth, must be in range 1-64");
                }
                break;

            case 'w':
                warmups = (size_t) parseNumber(optarg);
                break;

            case 'r':
                repetitions = (size_t) parseNumber(optarg);
                break;

            case 'v':
                filename = optarg;
                break;
        }
    }

    if (controllerId == 0)
    {
        throw string("No controller specified!");
    }

    if (numBlocks == 0)
    {
        throw string("No length is specified!");
    }
}

