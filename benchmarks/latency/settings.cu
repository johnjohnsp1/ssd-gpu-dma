#include <cuda.h>
#include "settings.h"
#include <nvm_types.h>
#include <nvm_cmd.h>
#include <nvm_aq.h>
#include <iomanip>
#include <sstream>
#include <string>
#include <cstdint>
#include <getopt.h>

using std::string;

static const struct option options[] = {
    { .name = "help", .has_arg = no_argument, .flag = nullptr, .val = 'h' },
    { .name = "nvm-ctrl", .has_arg = required_argument, .flag = nullptr, .val = 'c' },
    { .name = "ctrl", .has_arg = required_argument, .flag = nullptr, .val = 'c' },
    { .name = "nvm-controller", .has_arg = required_argument, .flag = nullptr, .val = 'c' },
    { .name = "controller", .has_arg = required_argument, .flag = nullptr, .val = 'c' },
    { .name = "nc", .has_arg = required_argument, .flag = nullptr, .val = 'c' },
    { .name = "cuda-device", .has_arg = required_argument, .flag = nullptr, .val = 'g' },
    { .name = "device", .has_arg = required_argument, .flag = nullptr, .val = 'g' },
    { .name = "cuda-gpu", .has_arg = required_argument, .flag = nullptr, .val = 'g' },
    { .name = "gpu", .has_arg = required_argument, .flag = nullptr, .val = 'g' },
    { .name = "no-gpu", .has_arg = no_argument, .flag = nullptr, .val = 'g' },
    { .name = "nvm-namespace", .has_arg = required_argument, .flag = nullptr, .val = 'i' },
    { .name = "namespace", .has_arg = required_argument, .flag = nullptr, .val = 'i' },
    { .name = "ns", .has_arg = required_argument, .flag = nullptr, .val = 'i' },
    { .name = "adapter", .has_arg = required_argument, .flag = nullptr, .val = 'a' },
    { .name = "num-blocks", .has_arg = required_argument, .flag = nullptr, .val = 'n' },
    { .name = "block-count", .has_arg = required_argument, .flag = nullptr, .val = 'n' },
    { .name = "blocks", .has_arg = required_argument, .flag = nullptr, .val = 'n' },
    { .name = "length", .has_arg = required_argument, .flag = nullptr, .val = 'n' },
    { .name = "len", .has_arg = required_argument, .flag = nullptr, .val = 'n' },
    { .name = "bc", .has_arg = required_argument, .flag = nullptr, .val = 'n' },
    { .name = "start", .has_arg = required_argument, .flag  = nullptr, .val = 'o' },
    { .name = "start-block", .has_arg = required_argument, .flag  = nullptr, .val = 'o' },
    { .name = "block-offset", .has_arg = required_argument, .flag  = nullptr, .val = 'o' },
    { .name = "offset", .has_arg = required_argument, .flag  = nullptr, .val = 'o' },
    { .name = "offs", .has_arg = required_argument, .flag  = nullptr, .val = 'o' },
    { .name = "sb", .has_arg = required_argument, .flag  = nullptr, .val = 'o' },
    { .name = "num-queues", .has_arg = required_argument, .flag = nullptr, .val = 'q' },
    { .name = "queue-count", .has_arg = required_argument, .flag = nullptr, .val = 'q' },
    { .name = "queues", .has_arg = required_argument, .flag = nullptr, .val = 'q' },
    { .name = "nq", .has_arg = required_argument, .flag = nullptr, .val = 'q' },
    { .name = "queue-depth", .has_arg = required_argument, .flag = nullptr, .val = 'd' },
    { .name = "qd", .has_arg = required_argument, .flag = nullptr, .val = 'd' },
    { .name = "queue-length", .has_arg = required_argument, .flag = nullptr, .val = 'd' },
    { .name = "depth", .has_arg = required_argument, .flag = nullptr, .val = 'd' },
    { .name = "local-sq", .has_arg = no_argument, .flag = nullptr, .val = 2 },
    { .name = "warmups", .has_arg = required_argument, .flag = nullptr, .val = 'w' },
    { .name = "repeat", .has_arg = required_argument, .flag = nullptr, .val = 'r' },
    { .name = "repetitions", .has_arg = required_argument, .flag = nullptr, .val = 'r' },
    { .name = "reps", .has_arg = required_argument, .flag = nullptr, .val = 'r' },
    { .name = "count", .has_arg = required_argument, .flag = nullptr, .val = 'r' },
    { .name = "verify", .has_arg = required_argument, .flag = nullptr, .val = 'v' },
    { .name = "pattern", .has_arg = required_argument, .flag = nullptr, .val = 'p' },
    { .name = "mode", .has_arg = required_argument, .flag = nullptr, .val = 'p' },
    { .name = "write", .has_arg = no_argument, .flag = nullptr, .val = 1 },
    { .name = "statistics", .has_arg = no_argument, .flag = nullptr, .val = 's' },
    { .name = "stats", .has_arg = no_argument, .flag = nullptr, .val = 's' },
    { .name = nullptr, .has_arg = no_argument, .flag = nullptr, .val = 0 }
};



static string usageString(const char* name)
{
    return name + string(": --ctrl <id> --blocks <count> [--gpu <id>] [--queues <number>] [--depth <number>] [--pattern {random|sequential|linear}]");
}


static void argInfo(std::ostringstream& s, const string& name, const string& argument, const string& info)
{
    using namespace std;
    s << "    " << left 
        << setw(16) << ((name.length() == 1 ? "-" : "--") + name) 
        << setw(16) << argument
        << setw(40) << info << endl;
}

static void modeInfo(std::ostringstream& s, const string& name, const string& info)
{
    using namespace std;
    s << "    " << left 
        << setw(16) << name
        << setw(56) << info << endl;
}

static void argInfo(std::ostringstream& s, const string& name, const string& info)
{
    argInfo(s, name, "", info);
}


static string helpString(const char* name)
{
    std::ostringstream s;

    s << usageString(name) << std::endl;
    s << std::endl << "Arguments" << std::endl;
    argInfo(s, "help", "show this help");
    argInfo(s, "ctrl", "id", "NVM controller identifier");
    argInfo(s, "adapter", "number", "DIS adapter number (default is 0)");
    argInfo(s, "namespace", "id", "specify NVM namespace (default is 1)");
    argInfo(s, "blocks", "count", "specify number of blocks");
    argInfo(s, "offset", "count", "specify start block (default is 0)");
    argInfo(s, "queues", "number", "specify number of queues (default is 1)");
    argInfo(s, "depth", "number", "specify number of commands per queue (default is 32)");
    argInfo(s, "repeat", "repetitions", "number of times to repeat measurement (default is 1000)");
    argInfo(s, "gpu", "[device]", "select GPUDirect capable CUDA device (default is none)");
    argInfo(s, "verify", "path", "use file to verify transfer");
    argInfo(s, "write", "write instead of read (WARNING! Will destroy data on disk)");
    argInfo(s, "local-sq", "host submission queue and PRP lists in local memory");
    argInfo(s, "stats", "print latency statistics to stdout");
    argInfo(s, "pattern", "mode", "specify access pattern (default is sequential)");

    s << std::endl;
    s << "Access patterns:" << std::endl;
    modeInfo(s, "sequential", "overlapping sequential access pattern, queues access same blocks");
    modeInfo(s, "linear", "linear sequential access pattern, queues do not access same blocks");
    modeInfo(s, "random", "random access pattern, individual commands start at a random offset");

    return s.str();
}


static AccessPattern parsePattern(const string& s)
{
    if (s == "linear")
    {
        return AccessPattern::LINEAR;
    }
    else if (s == "seq" || s == "sequential")
    {
        return AccessPattern::SEQUENTIAL;
    }
    else if (s == "random")
    {
        return AccessPattern::RANDOM;
    }

    throw string("Invalid access pattern: " + s);
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
    warmups = 0;
    repetitions = 1000;
    numQueues = 1;
    queueDepth = 32;
    numBlocks = 0;
    startBlock = 0;
    pattern = SEQUENTIAL;
    filename = nullptr;
    write = false;
    remote = true;
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

    while ((option = getopt_long(argc, argv, ":hc:g:i:a:n:o:q:d:w:r:v:p:s", options, &index)) != -1)
    {
        switch (option)
        {
            case '?':
                throw string("Unknown option: `") + argv[optind - 1] + string("'");

            case ':':
                throw string("Missing argument for option ") + argv[optind - 1];

            case 1:
                write = true;
                break;

            case 2:
                remote = false;
                break;

            case 'h':
                throw helpString(argv[0]);

            case 'p':
                pattern = parsePattern(optarg);
                break;

            case 'c':
                controllerId = (uint64_t) parseNumber(optarg, 16);
                break;

            case 'g':
                if (optarg != nullptr)
                {
                    cudaDevice = (int) parseNumber(optarg, 10);
                    if (cudaDevice < 0 || cudaDevice >= maxCudaDevice())
                    {
                        throw string("Invalid CUDA device: ") + optarg;
                    }
                }
                else
                {
                    cudaDevice = -1;
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
                    throw string("Invalid queue depth, must be in range 1-63");
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

            case 's':
                stats = true;
                break;
        }
    }

    if (controllerId == 0)
    {
        throw string("No controller specified!");
    }

    if (numBlocks == 0)
    {
        throw string("No block count is specified!");
    }

    if (pattern == AccessPattern::RANDOM && filename != nullptr)
    {
        throw string("Can not verify random access pattern!");
    }

    if (write && filename != nullptr)
    {
        throw string("Can not write and verify!");
    }
}

