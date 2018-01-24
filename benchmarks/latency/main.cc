#include "settings.h"
#include "buffer.h"
#include "ctrl.h"
#include "queue.h"
#include "barrier.h"
#include <nvm_types.h>
#include <nvm_ctrl.h>
#include <nvm_error.h>
#include <nvm_util.h>
#include <nvm_queue.h>
#include <nvm_cmd.h>
#include <stdexcept>
#include <vector>
#include <memory>
#include <algorithm>
#include <thread>
#include <chrono>
#include <string>
#include <limits>
#include <cstring>
#include <cstdlib>
#include <sisci_api.h>

using std::string;
using std::runtime_error;
using std::make_shared;
using std::thread;


typedef std::chrono::duration<double, std::micro> mtime;

struct Time
{
    uint16_t    depth;
    mtime       time;
    
    Time(uint16_t depth, mtime time)
        : depth(depth), time(time) {}
};


typedef std::vector<Time> Times;


static size_t createQueues(const Controller& ctrl, Settings& settings, QueueList& queues)
{
    const size_t pageSize = ctrl.info.page_size;
    const size_t blockSize = ctrl.ns.lba_data_size;

    const size_t transferPages = NVM_PAGE_ALIGN(settings.numBlocks * blockSize, pageSize) / pageSize;
    const size_t pagesPerQueue = transferPages / ctrl.numQueues;

    srand(settings.startBlock);

    size_t dataPages = 0;

    for (uint16_t i = 0; i < ctrl.numQueues; ++i)
    {
        auto queue = make_shared<Queue>(ctrl, settings.adapter, settings.segmentId++, i+1, settings.queueDepth);
        size_t pageOff = pagesPerQueue * i;

        switch (settings.pattern)
        {
            case AccessPattern::REPEAT:
                dataPages += prepareRange(queue->transfers, ctrl, dataPages, settings.startBlock, settings.numBlocks, false);
                break;

            case AccessPattern::SEQUENTIAL:
                if (i == ctrl.numQueues - 1)
                {
                    dataPages += prepareRange(queue->transfers, ctrl, pageOff,
                            NVM_PAGE_TO_BLOCK(pageSize, blockSize, pageOff), settings.numBlocks - NVM_PAGE_TO_BLOCK(pageSize, blockSize, pageOff), false);
                }
                else
                {
                    dataPages += prepareRange(queue->transfers, ctrl, pageOff,
                            NVM_PAGE_TO_BLOCK(pageSize, blockSize, pageOff), NVM_PAGE_TO_BLOCK(pageSize, blockSize, pagesPerQueue), false);
                }
                break;

            case AccessPattern::RANDOM:
                break;
        }

        queues.push_back(queue);
    }

    return dataPages;
}


static void benchmark(const QueueList& queues, const BufferPtr& buffer, const Settings& settings);



//static void dumpMemory(const BufferPtr& buffer, bool ascii)
//{
//    uint8_t* ptr = (uint8_t*) buffer->vaddr;
//    size_t byte = 0;
//    size_t size = buffer->page_size * buffer->n_ioaddrs;
//    while (byte < size)
//    {
//        fprintf(stderr, "%8lx: ", byte);
//        for (size_t n = byte + (ascii ? 0x80 : 0x20); byte < n; ++byte)
//        {
//            uint8_t value = ptr[byte];
//            if (ascii)
//            {
//                if ( !(0x20 <= value && value <= 0x7e) )
//                {
//                    value = ' ';
//                }
//                fprintf(stdout, "%c", value);
//            }
//            else
//            {
//                fprintf(stdout, " %02x", value);
//            }
//        }
//        fprintf(stdout, "\n");
//    }
//}


static void verify(const Controller& ctrl, const QueueList& queues, const BufferPtr& buffer, const Settings& settings)
{
    size_t fileSize = settings.numBlocks * ctrl.ns.lba_data_size;

    void* ptr = malloc(fileSize);
    if (ptr == nullptr)
    {
        throw runtime_error(string("Failed to allocate local buffer: ") + strerror(errno));
    }

    FILE* fp = fopen(settings.filename, "r");
    if (fp == nullptr)
    {
        free(ptr);
        throw runtime_error(string("Failed to open file: ") + strerror(errno));
    }

    size_t actualSize = fread(ptr, 1, fileSize, fp);
    fclose(fp);

    if (actualSize != fileSize)
    {
        fprintf(stderr, "WARNING: Verification file differs in size!\n");
    }

    switch (settings.pattern)
    {
        case AccessPattern::REPEAT:
            for (const auto& queue: queues)
            {
                const auto& start = *queue->transfers.begin();

                if (memcmp(ptr, NVM_DMA_OFFSET(buffer, start.startPage), actualSize) != 0)
                {
                    free(ptr);
                    throw runtime_error("File differs!");
                }
            }
            break;

        case AccessPattern::SEQUENTIAL:
            if (memcmp(ptr, buffer->vaddr, actualSize) != 0)
            {
                free(ptr);
                throw runtime_error("File differs!");
            }
            break;

        case AccessPattern::RANDOM:
            free(ptr);
            throw runtime_error("Unable to verify random blocks!");
            break;
    }

    free(ptr);
}



int main(int argc, char** argv)
{
    Settings settings;

    // Parse command line arguments
    try
    {
        settings.parseArguments(argc, argv);
    }
    catch (const string& s)
    {
        fprintf(stderr, "%s\n", s.c_str());
        return 1;
    }

    sci_error_t err;
    SCIInitialize(0, &err);
    if (err != SCI_ERR_OK)
    {
        fprintf(stderr, "Something went wrong: %s\n", SCIGetErrorString(err));
        return 1;
    }

    try
    {
        fprintf(stderr, "Resetting controller...\n");
        Controller ctrl(settings.controllerId, settings.adapter, settings.segmentId++, settings.nvmNamespace, settings.numQueues);

        fprintf(stderr, "page size = 0x%zx, block size = 0x%zx\n", ctrl.info.page_size, ctrl.ns.lba_data_size);

        settings.numQueues = ctrl.numQueues;

        fprintf(stderr, "Creating %u queues with depth %zu...\n", ctrl.numQueues, settings.queueDepth);
        QueueList queues;
        size_t numPages = createQueues(ctrl, settings, queues);

        fprintf(stderr, "Creating buffer (%zu pages)...\n", numPages);
        BufferPtr buffer = createBuffer(ctrl.ctrl, settings.adapter, settings.segmentId++, numPages * ctrl.ctrl->page_size, settings.cudaDevice);

        benchmark(queues, buffer, settings);

        if (settings.filename != nullptr && settings.pattern != AccessPattern::RANDOM)
        {
            verify(ctrl, queues, buffer, settings);
        }

        //dumpMemory(buffer, false);
    }
    catch (const runtime_error& e)
    {
        fprintf(stderr, "Unexpected error: %s\n", e.what());
        return 1;
    }

    SCITerminate();
    return 0;
}




static Time sendWindow(QueuePtr& queue, TransferPtr& from, const TransferPtr& to, const BufferPtr& buffer, uint32_t ns)
{
    size_t numCommands = 0;

    // Fill up to queue depth with commands
    for (numCommands = 0; numCommands < queue->depth && from != to; ++numCommands, ++from)
    {
        nvm_cmd_t* cmd = nvm_sq_enqueue(&queue->sq);
        if (cmd == nullptr)
        {
            throw runtime_error(string("Queue is full, should not happen!"));
        }

        const Transfer& t = *from;
        void* prpListPtr = NVM_DMA_OFFSET(queue->sq_mem, 1 + numCommands);
        uint64_t prpListAddr = queue->sq_mem->ioaddrs[1 + numCommands];
        
        nvm_cmd_header(cmd, t.write ? NVM_IO_WRITE : NVM_IO_READ, ns);
        nvm_cmd_rw_blks(cmd, t.startBlock, t.numBlocks);
        nvm_cmd_data(cmd, buffer->page_size, t.numPages, prpListPtr, prpListAddr, &buffer->ioaddrs[t.startPage]);
    }

    // Get current time before submitting
    auto before = std::chrono::high_resolution_clock::now();
    nvm_sq_submit(&queue->sq);

    // Wait for all completions
    for (size_t i = 0; i < numCommands; ++i)
    {
        nvm_cpl_t* cpl;
        while ((cpl = nvm_cq_dequeue(&queue->cq)) == nullptr)
        {
            std::this_thread::yield();
        }

        nvm_sq_update(&queue->sq);

        if (!NVM_ERR_OK(cpl))
        {
            fprintf(stderr, "%u: %s\n", queue->no, nvm_strerror(NVM_ERR_STATUS(cpl)));
        }

        nvm_cq_update(&queue->cq);
    }

    // Get current time after all commands completed
    auto after = std::chrono::high_resolution_clock::now();

    return Time(numCommands, after - before);
}



static void measure(QueuePtr queue, const BufferPtr buffer, Times* times, const Settings& settings, Barrier* barrier)
{
    for (size_t i = 0; i < settings.repetitions; ++i)
    {
        TransferPtr transferPtr = queue->transfers.cbegin();
        const TransferPtr transferEnd = queue->transfers.cend();

        barrier->wait();
        
        auto time = sendWindow(queue, transferPtr, transferEnd, buffer, settings.nvmNamespace);

        times->push_back(time);
    }
}



static void printStatistics(const QueuePtr& queue, const Times& times)
{
    double min = std::numeric_limits<double>::max();
    double max = std::numeric_limits<double>::min();
    double avg = 0;

    for (const auto& t: times)
    {
        const auto current = t.time.count();

        if (current < min)
        {
            min = current;
        }
        if (current > max)
        {
            max = current;
        }

        avg += current;
    }

    avg /= times.size();

    size_t blocks = 0;
    for (const auto& t: queue->transfers)
    {
        blocks += t.numBlocks;
    }

    fprintf(stderr, "Queue #%u qd=%zu blocks=%zu count=%zu min=%.1f avg=%.1f max=%.1f\n", 
            queue->no,  queue->depth, blocks, times.size(), min, avg, max);
}



static void benchmark(const QueueList& queues, const BufferPtr& buffer, const Settings& settings)
{
    Times times[queues.size()];
    thread threads[queues.size()];

    memset(buffer->vaddr, 0x00, buffer->page_size * buffer->n_ioaddrs);
    Barrier barrier(queues.size());

    for (size_t i = 0; i < queues.size(); ++i)
    {
        threads[i] = thread(measure, queues[i], buffer, &times[i], settings, &barrier);
    }

    fprintf(stderr, "Running benchmark...\n");

    for (size_t i = 0; i < queues.size(); ++i)
    {
        threads[i].join();
        printStatistics(queues[i], times[i]);
    }
}
