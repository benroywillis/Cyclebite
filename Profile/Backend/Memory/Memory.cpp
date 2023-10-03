#include "Util/Exceptions.h"
#include "Util/IO.h"
#include "Memory.h"
#include "Epoch.h"
#include "CodeInstance.h"
#include "IO.h"
#include "Processing.h"

using namespace std;
using json = nlohmann::json;
using namespace llvm;

namespace Cyclebite::Profile::Backend::Memory
{
    /// Timing information
    struct timespec start, end;

    /// Maps critical edges to the codesections they enter
    /// Holds edges that transition from one part of the program to the other
    /// These edges are encoded as pairs of block IDs, first entry is the source, second is sink
    map<pair<int64_t, int64_t>, set<shared_ptr<CodeSection>, UIDCompare>> epochBoundaries;
    /// holds all epochs that have been observed
    set<shared_ptr<Epoch>, UIDCompare> epochs;
    /// holds all sets of basic blocks that should be observed in an epoch at some point in the profile
    map<uint64_t, set<int64_t>> taskCandidates;
    /// Maps instructions to their working set tuples
    /// These mappings are used in the grammar tool to figure out which load instructions are touching critical pieces of memory
    map<int64_t, set<MemTuple, MTCompare>> instToTuple;

    /// Holds all CodeSections
    /// A code section is a unique set of basic block IDs ie a codesection may map to multiple kernels
    set<shared_ptr<CodeSection>, UIDCompare> codeSections;
    /// Holds all unique block sets in the program
    set<shared_ptr<Kernel>, UIDCompare> blockSets;
    /// Maps a kernel to its dominators
    /// Dominators are kernels that must happen before a given kernel can execute
    map<shared_ptr<Kernel>, set<int64_t>> dominators;

    /// Holds all task candidates that are read from the input kernel file
    std::set<std::shared_ptr<Kernel>, UIDCompare> kernels;
    /// A set of block IDs that have already executed been seen in the profile
    set<int64_t> executedBlocks;
    /// Holds the current kernel instance(s)
    shared_ptr<Epoch> currentEpoch;
    /// Remembers the block seen before the current so we can dynamically find kernel exits
    int64_t lastBlock;
    /// On/off switch for the profiler
    bool memoryActive = false;
    /// Counter tracking how much memory has been consumed by the profiler
    uint64_t bytesBitten;

    void updateBittenBytes()
    {
        uint64_t bittenBytes = 0;
        for( const auto& e : epochs )
        {
            bittenBytes += sizeof(Epoch) + sizeof(e->memoryData.rTuples.size())*sizeof(MemTuple) + sizeof(e->memoryData.wTuples.size())*sizeof(MemTuple);
        }
        if( bittenBytes > bytesBitten )
        {
            bytesBitten = bittenBytes;
        }
    }

    extern "C"
    {
        void __Cyclebite__Profile__Backend__MemoryDestroy()
        {
            clock_gettime(CLOCK_MONOTONIC, &end);
            updateBittenBytes();
            spdlog::info( "MEMORYPROFILETIME: "+to_string(CalculateTime(&start, &end))+"s");
            spdlog::info( "MEMORYPROFILESPACE: "+to_string(bytesBitten));
            updateBittenBytes();
            memoryActive = false;
            // this is an implicit exit, so store the current iteration information to where it belongs
            epochs.insert(currentEpoch);
            ProcessEpochBoundaries();
            GenerateMemoryRegions();
            GenerateTaskGraph();
            GenerateTaskOnlyTaskGraph();
            OutputKernelInstances();
        }

        void __Cyclebite__Profile__Backend__MemoryIncrement(uint64_t a)
        {
            // if the profile is not active, we return
            if (!memoryActive)
            {
                return;
            }
            auto crossedEdge = pair(lastBlock, (int64_t)a);
            // exiting sections
            if (epochBoundaries.find(crossedEdge) != epochBoundaries.end())
            {
                currentEpoch->exits[lastBlock].insert((int64_t)a);
                epochs.insert(currentEpoch);
                currentEpoch = make_shared<Epoch>();
                currentEpoch->updateBlocks((int64_t)a);
                currentEpoch->entrances[lastBlock].insert((int64_t)a);
            }
            else
            {
                currentEpoch->updateBlocks((int64_t)a);
            }
#if NONKERNEL
            executedBlocks.insert((int64_t)a);
#endif
            lastBlock = (int64_t)a;
        }

        void __Cyclebite__Profile__Backend__MemoryStore(void *address, int64_t valueID, uint64_t datasize)
        {
            static MemTuple mt;
            mt.type = __TA_MemType::Writer;
            if (!memoryActive)
            {
                return;
            }
            mt.base = (uint64_t)address;
            mt.offset = (uint32_t)datasize;
            mt.refCount = 0;
#if NONKERNEL
            merge_tuple_set(currentEpoch.wTuples, mt);
#else
            if( currentEpoch )
            {
                merge_tuple_set(currentEpoch->memoryData.wTuples, mt);
            }
#endif
            // instruction tuples
            if( instToTuple.find(valueID) == instToTuple.end() )
            {
                instToTuple[valueID].insert(mt);
            }
            else
            {
                merge_tuple_set(instToTuple.at(valueID), mt);
            }
            updateBittenBytes();
        }

        void __Cyclebite__Profile__Backend__MemoryLoad(void *address, int64_t valueID, uint64_t datasize)
        {
            static MemTuple mt;
            mt.type = __TA_MemType::Reader;
            if (!memoryActive)
            {
                return;
            }
            mt.base = (uint64_t)address;
            mt.offset = (uint32_t)datasize;
            mt.refCount = 0;
#if NONKERNEL
            merge_tuple_set(currentEpoch.rTuples, mt);
#else
            if( currentEpoch )
            {
                merge_tuple_set(currentEpoch->memoryData.rTuples, mt);
            }
#endif
            // instruction tuples
            if( instToTuple.find(valueID) == instToTuple.end() )
            {
                instToTuple[valueID].insert(mt);
            }
            else
            {
                merge_tuple_set(instToTuple.at(valueID), mt);
            }
            updateBittenBytes();
        }

        void __Cyclebite__Profile__Backend__MemoryInit(uint64_t a)
        {
            bytesBitten = 0;
            ReadKernelFile();
            try
            {
                FindEpochBoundaries();
            }
            catch (AtlasException &e)
            {
                spdlog::critical(e.what());
                exit(EXIT_FAILURE);
            }
            currentEpoch = make_shared<Epoch>();
            currentEpoch->updateBlocks((int64_t)a);
            currentEpoch->entrances[(int64_t)a].insert((int64_t)a);

            while( clock_gettime(CLOCK_MONOTONIC, &start) ) {}
            memoryActive = true;
            lastBlock = (int64_t)a;
        }

        void __Cyclebite__Profile__Backend__MemoryCpy(void* ptr_src, void* ptr_snk, uint64_t dataSize)
        {
            MemTuple mt;
            mt.type = __TA_MemType::Memcpy;
            mt.base = (uint64_t)ptr_src;
            mt.offset = (uint32_t)dataSize-1;
            mt.refCount = 0;
            if( currentEpoch )
            {
                merge_tuple_set(currentEpoch->memoryData.rTuples, mt);
            }
            mt.base = (uint64_t)ptr_snk;
            if( currentEpoch )
            {
                merge_tuple_set(currentEpoch->memoryData.wTuples, mt);
            }
            updateBittenBytes();
        }

        void __Cyclebite__Profile__Backend__MemoryMov(void* ptr_src, void* ptr_snk, uint64_t dataSize)
        {
            MemTuple mt;
            mt.type = __TA_MemType::Memmov;
            mt.base = (uint64_t)ptr_src;
            mt.offset = (uint32_t)dataSize-1;
            mt.refCount = 0;
            if( currentEpoch )
            {
                merge_tuple_set(currentEpoch->memoryData.rTuples, mt);
            }
            mt.base = (uint64_t)ptr_snk;
            if( currentEpoch )
            {
                merge_tuple_set(currentEpoch->memoryData.wTuples, mt);
            }
            updateBittenBytes();
        }

        void __Cyclebite__Profile__Backend__MemorySet(void* ptr, uint64_t dataSize)
        {
            MemTuple mt;
            mt.type = __TA_MemType::Memset;
            mt.base = (uint64_t)ptr;
            mt.offset = (uint32_t)dataSize-1;
            mt.refCount = 0;
            if( currentEpoch )
            {
                merge_tuple_set(currentEpoch->memoryData.wTuples, mt);
            }
            updateBittenBytes();
        }

        void __Cyclebite__Profile__Backend__MemoryMalloc(void* ptr, uint64_t offset)
        {
            if( currentEpoch )
            {
                MemTuple mt;
                mt.type = __TA_MemType::Malloc;
                mt.base = (uint64_t)ptr;
                mt.offset = (uint32_t)offset-1;
                currentEpoch->malloc_ptrs.insert(mt);
            }
            updateBittenBytes();
        }

        void __Cyclebite__Profile__Backend__MemoryFree(void* ptr)
        {
            if( currentEpoch )
            {
                currentEpoch->free_ptrs.insert((int64_t)ptr);
            }
        }
    }
} // namespace Cyclebite::Backend::BackendMemory