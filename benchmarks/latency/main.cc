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


static void transferRange(const Controller& ctrl, TransferList& list, size_t startBlock, size_t numBlocks, bool write)
{
    const size_t blockSize = ctrl.ns.lba_data_size;
    const size_t pageSize = ctrl.info.page_size;
    const size_t transferPages = ctrl.info.max_data_pages;
    
    size_t transferBlocks = NVM_PAGE_TO_BLOCK(pageSize, blockSize, transferPages);

    while (numBlocks != 0)
    {
        transferBlocks = std::min(transferBlocks, numBlocks);

        Transfer t;
        t.write = write;
        t.startBlock = startBlock;
        t.numBlocks = transferBlocks;
        t.pages = NVM_BLOCK_TO_PAGE(pageSize, blockSize, t.numBlocks);

        list.push_back(t);

        startBlock += transferBlocks;
        numBlocks -= transferBlocks;
    }
}



static void fillRandom(const Controller& ctrl, TransferList& list, size_t numBlocks, bool write)
{
    const size_t blockSize = ctrl.ns.lba_data_size;
    const size_t pageSize = ctrl.ctrl->page_size;
    size_t transferBlocks = NVM_PAGE_TO_BLOCK(pageSize, blockSize, ctrl.info.max_data_pages);

    size_t startBlock = rand() % ctrl.ns.size;

    while (numBlocks != 0)
    {
        transferBlocks = std::min(transferBlocks, numBlocks);

        Transfer t;
        t.write = write;
        t.startBlock = startBlock;
        t.numBlocks = transferBlocks;
        t.pages = NVM_BLOCK_TO_PAGE(pageSize, blockSize, t.numBlocks);

        list.push_back(t);

        startBlock += transferBlocks;
        numBlocks -= transferBlocks;
    }
}



static size_t createQueues(const Controller& ctrl, Settings& settings, QueueList& queues)
{
    const size_t blockSize = ctrl.ns.lba_data_size;
    const size_t pageSize = ctrl.ctrl->page_size;

    size_t startBlock = settings.startBlock;
    size_t numBlocks = settings.numBlocks;

    const size_t numPages = NVM_PAGE_ALIGN(numBlocks * blockSize, pageSize) / pageSize;
    const size_t pagesPerQueue = numPages / ctrl.numQueues;

    size_t totalPages = 0;

    srand(settings.startBlock);

    for (uint16_t i = 0; i < ctrl.numQueues; ++i)
    {
        auto queue = make_shared<Queue>(ctrl, settings.adapter, settings.segmentId++, i+1, settings.queueDepth);

        switch (settings.pattern)
        {
            case AccessPattern::REPEAT:
                queue->startPage = totalPages;
                queue->endPage = totalPages + numPages;
                totalPages += numPages;

                transferRange(ctrl, queue->transfers, startBlock, numBlocks, false);

                fprintf(stderr, "\tQueue #%u starts at block %zu and ends at block %zu (page %zu)\n", 
                        queue->no, startBlock, startBlock + numBlocks, queue->startPage);
                break;

            case AccessPattern::SEQUENTIAL:
                queue->startPage = i * pagesPerQueue;
                queue->endPage = (i + 1) * pagesPerQueue;
                totalPages = numPages;

                startBlock = settings.startBlock + NVM_PAGE_TO_BLOCK(pageSize, blockSize, pagesPerQueue * i);
                numBlocks = NVM_PAGE_TO_BLOCK(pageSize, blockSize, pagesPerQueue);

                if (i + 1 == ctrl.numQueues)
                {
                    numBlocks = settings.numBlocks - startBlock;
                }

                transferRange(ctrl, queue->transfers, startBlock, numBlocks, false);

                fprintf(stderr, "\tQueue #%u starts at block %zu and ends at block %zu (page %zu)\n", 
                        queue->no, startBlock, startBlock + numBlocks, queue->startPage);
                break;

            case AccessPattern::RANDOM:
                queue->startPage = i;
                queue->endPage = ctrl.info.max_data_pages;
                totalPages = ctrl.numQueues;

                fillRandom(ctrl, queue->transfers, numBlocks, false);
                fprintf(stderr, "\tQueue #%u random blocks (page %zu)\n", queue->no, queue->startPage);
                break;
        }

        queues.push_back(queue);
    }

    return totalPages;
}


static void benchmark(const QueueList& queues, const BufferPtr& buffer, const Settings& settings);



static void dumpMemory(const BufferPtr& buffer, bool ascii)
{
    uint8_t* ptr = (uint8_t*) buffer->vaddr;
    size_t byte = 0;
    size_t size = buffer->page_size * buffer->n_ioaddrs;
    while (byte < size)
    {
        fprintf(stderr, "%8lx: ", byte);
        for (size_t n = byte + (ascii ? 0x80 : 0x20); byte < n; ++byte)
        {
            uint8_t value = ptr[byte];
            if (ascii)
            {
                if ( !(0x20 <= value && value <= 0x7e) )
                {
                    value = ' ';
                }
                fprintf(stdout, "%c", value);
            }
            else
            {
                fprintf(stdout, " %02x", value);
            }
        }
        fprintf(stdout, "\n");
    }
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







static void measure(QueuePtr queue, BufferPtr buffer, Times* times, const Settings& settings, Barrier* barrier)
{
    // Create local copy of IO addresses
    uint64_t ioAddresses[queue->endPage - queue->startPage];
    for (size_t i = 0; i < (queue->endPage - queue->startPage); ++i)
    {
        switch (settings.pattern)
        {
            case AccessPattern::REPEAT:
            case AccessPattern::SEQUENTIAL:
                ioAddresses[i] = buffer->ioaddrs[queue->startPage + i];
                break;

            case AccessPattern::RANDOM:
                ioAddresses[i] = buffer->ioaddrs[queue->startPage];
                break;
        }
    }

    for (size_t i = 0; i < settings.repetitions; ++i)
    {
        barrier->wait();

        auto transferIt = queue->transfers.begin();
        auto transferEnd = queue->transfers.end();
        size_t pageIdx = 0;

        while (transferIt != transferEnd)
        {
            size_t n;
            for (n = 0; n < queue->depth && transferIt != transferEnd; ++n)
            {
                nvm_cmd_t* cmd = nvm_sq_enqueue(&queue->sq);

                nvm_cmd_header(cmd, transferIt->write ? NVM_IO_WRITE : NVM_IO_READ, settings.nvmNamespace);
                nvm_cmd_rw_blks(cmd, transferIt->startBlock, transferIt->numBlocks);

                pageIdx += nvm_cmd_data(cmd, buffer->page_size, transferIt->pages,
                        NVM_DMA_OFFSET(queue->sq_mem, 1 + n), queue->sq_mem->ioaddrs[1 + n], ioAddresses);

                ++transferIt;
            }

            auto before = std::chrono::high_resolution_clock::now();

            nvm_sq_submit(&queue->sq);

            for (size_t i = 0; i < n; ++i)
            {
                nvm_cpl_t* cpl;
                while ((cpl = nvm_cq_dequeue(&queue->cq)) == NULL)
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

            auto after = std::chrono::high_resolution_clock::now();
            
            times->push_back(Time(n, after - before));
        }
    }
}



static void printStatistics(const QueuePtr& queue, const Times& times)
{
    double min = std::numeric_limits<double>::max();
    double max = std::numeric_limits<double>::min();
    double avg = 0;

    for (auto& t: times)
    {
        auto current = t.time.count();

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

    fprintf(stderr, "Queue #%u qd=%zu count=%zu min=%.1f avg=%.1f max=%.1f\n", 
            queue->no, queue->depth, times.size(), min, avg, max);
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

