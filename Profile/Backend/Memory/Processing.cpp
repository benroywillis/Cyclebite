//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "Processing.h"
#include "Epoch.h"
#include "Kernel.h"
#include <fstream>
#include <cstdlib>

using namespace std;

// maps an epoch ID to its RAW (first) and WAW (second) dependencies
map<uint64_t, pair<set<uint64_t>, set<uint64_t>>> Cyclebite::Profile::Backend::Memory::TaskCommunications;

namespace Cyclebite::Profile::Backend::Memory
{
    void FindEpochBoundaries()
    {
        // first step, find critical edges for kernel entrances
        for (auto k : blockSets)
        {
            // for now we are only interested in highest-level kernels
            if( !k->parents.empty() )
            {
                continue;
            }
            for (auto e : k->entrances)
            {
                for (auto entry : e.second)
                {
                    epochBoundaries[pair(e.first, entry)].insert(k);
                }
            }
            for (auto e : k->exits)
            {
                for (auto entry : e.second)
                {
                    epochBoundaries[pair(e.first, entry)].insert(k);
                }
            }
        }
    }

    void ProcessEpochBoundaries()
    {
        // map epochs to their kernels, if possible
        for( const auto& instance : epochs )
        {
            // match the epoch to a kernel
            for( const auto& epoch : taskCandidates )
            {
                set<int64_t> overlap;
                for( const auto& block : instance->blocks )
                {
                    if( epoch.second.find(block) != epoch.second.end() )
                    {
                        overlap.insert(block);
                    }
                }
                // overlap must be 50% or more
                if( ((float)overlap.size() / (float)instance->blocks.size()) > EPOCH_KERNEL_OVERLAP )
                {
                    instance->kernel = *kernels.find((int)epoch.first);
                    break;
                }
            }

            // 2. process memory regions
            // a.) a memory region must be larger than the minimum in order for us to care about it
            uint64_t totalOffset = 0;
            for( const auto& r : instance->memoryData.rTuples )
            {
                totalOffset += r.offset+1;
            }
            if( totalOffset < MIN_MEMORY_OFFSET )
            {
                instance->memoryData.rTuples.clear();
            }
            totalOffset = 0;
            for( const auto& w : instance->memoryData.wTuples )
            {
                totalOffset += w.offset+1;
            }
            if( totalOffset < MIN_MEMORY_OFFSET )
            {
                instance->memoryData.wTuples.clear();
            }
            // a.) epochs that allocated and then freed temporary arrays need to have those arrays taken out of their input/output working sets
            for( auto alloc : instance->malloc_ptrs )
            {
                bool match = false;
                for( auto free : instance->free_ptrs )
                {
                    if( (uint64_t)free == alloc.base )
                    {
                        match = true;
                        break;
                    }
                }
                if( match )
                {
                    remove_tuple_set(instance->memoryData.wTuples, alloc);
                    remove_tuple_set(instance->memoryData.rTuples, alloc);
                }
            }
        }
    }

    void GenerateMemoryRegions()
    {
        string csvString = "Hierarchy,Type,Start,End\n";
        for( const auto& instance : epochs )
        {
            for( const auto& rsection : instance->memoryData.rTuples )
            {
                csvString += to_string(instance->IID)+",READ,"+to_string(rsection.base)+","+to_string(rsection.base+(uint64_t)rsection.offset)+"\n";
            }
            for( const auto& wsection : instance->memoryData.wTuples )
            {
                csvString += to_string(instance->IID)+",WRITE,"+to_string(wsection.base)+","+to_string(wsection.base+(uint64_t)wsection.offset)+"\n";
            }
        }
        ofstream csvFile;
        if( getenv("CSV_FILE") )
        {
            csvFile = ofstream(getenv("CSV_FILE"));
        }
        else
        {
            csvFile = ofstream("MemoryFootprints_Hierarchies.csv");
        }
        csvFile << csvString;
        csvFile.close();
    }

    /// @brief Returns a set of MemTuples in "consumer" whose producers cannot be explained by "producer"
    ///
    /// @param producer     Set of write tuples
    /// @param consumer     Set of tuples (can be read or write) whose tuples may be explained (as in, the last writer to these addresses) by the mem tuples in "producer"
    /// @retval             Pair of values: first is a set of tuples from "consumer" which cannot be explained by the tuples in "producer", second is a bool indicating "true" if the returned set is different than the "consumer" argument
    pair<set<MemTuple, MTCompare>, bool> removeExplainedProducers(const set<MemTuple, MTCompare>& producer, const set<MemTuple, MTCompare>& consumer)
    {
        // this set contains all consumers whose producers cannot be explained with the tuples in producer
        set<MemTuple, MTCompare> unExplainedConsumers = consumer;
        bool changes = false;
        for( const auto& produced : producer )
        {
            for( const auto& consumed : consumer )
            {
                // in order for an overlap to be valid, the producer and consumer cannot have an __TA_TemporalAccess::WriteThenRead access pattern
                // these working sets represent a type of internal working set that creates a false dependency, often from modular operators that recur inside an application
                if( produced.AP == __TA_TemporalAccess::WriteThenRead && consumed.AP == __TA_TemporalAccess::WriteThenRead )
                {
                    // remove the working set from the unExplainedConsumers but don't mark it as a change
                    remove_tuple_set(unExplainedConsumers, consumed);
                }
                else
                {
                    auto overlap = mem_tuple_overlap(produced, consumed);
                    if( overlap.base + overlap.offset > 0 )
                    {
                        remove_tuple_set(unExplainedConsumers, overlap);
                        //if( overlap.offset > MIN_MEMORY_OFFSET )
                        //{
                            changes = true;
                        //}
                    }
                }
            }
        }
        return pair(unExplainedConsumers, changes);
    }

    void GenerateTaskCommunication()
    {
        // first check to see if CIFootprints is even the correct size
        if( epochs.size() < 2 )
        {
            spdlog::warn("No memory dependency information can be generated because there is only one code instance");
            return;
        }
        // walk backwards through the code instance footprints to generate RAW and WAW dependencies
        for( auto ti = epochs.rbegin(); ti != prev(epochs.rend()); ti++ )
        {
            auto instance = *ti;
            auto producer = next(ti);
            // first do RAW dependencies
            auto consumed = instance->memoryData.rTuples;
            while( !consumed.empty() && (producer != epochs.rend()) )
            {
                for( const auto& t : (*producer)->memoryData.wTuples )
                {
                    /*if( t.type == __TA_MemType::Memcpy )
                    {
                        // memcpy hides the true producer of the data of interest
                        // thus we have to find the read tuple that was copied (in the producer's read tuples) and "pass it on" to the consumer's read data
                        // for now, we just look inside the read data of the producer and look for a tuple of equal or greater size, and pass that on
                        MemTuple passItOn;
                        for( const auto& r : (*producer)->memoryData.rTuples )
                        {
                            if( mem_tuple_overlap(r, t).offset > 32 )
                            {
                                passItOn = r;
                                break;
                            }
                        }
                        if( passItOn.base > 0 )
                        {
                            merge_tuple_set(consumed, passItOn);
                        }
                    }
                    else if( t.type == __TA_MemType::Memmov )
                    {
                        // memmov is taking someone else's written data and moving it
                        // thus we are interested in "passing on" the original tuple (before the move) to the consumer's read data
                        MemTuple passItOn;
                        for( const auto& r : (*producer)->memoryData.rTuples )
                        {
                            if( r.offset >= t.offset )
                            {
                                passItOn = r;
                                break;
                            }
                        }
                        if( passItOn.base > 0 )
                        {
                            merge_tuple_set(consumed, passItOn);
                        }
                    }*/
                    // memset just writes to things, effectively making it the last writer of that data
                    // thus there is nothing to pass on to the consumer
                    // if a producer memset a memory region, the regular last-writer code will take care of this case
                }
                auto newTupleSet = removeExplainedProducers((*producer)->memoryData.wTuples, consumed);
                consumed = newTupleSet.first;
                if( newTupleSet.second )
                {
                    TaskCommunications[instance->IID].first.insert((*producer)->IID);
                }
                producer = next(producer);
            }
            // second do WAW dependencies
            auto producedLater = (*ti)->memoryData.wTuples;
            producer = next(ti);
            while( !producedLater.empty() && (producer != epochs.rend()) )
            {
                auto newTupleSet = removeExplainedProducers((*producer)->memoryData.wTuples, producedLater);
                producedLater = newTupleSet.first;
                if( newTupleSet.second )
                {
                    TaskCommunications[instance->IID].second.insert((*producer)->IID);
                }
                producer = next(producer);
            }
        }
    }

    map<int64_t, set<int64_t>> CombineStridedTuples()
    {
        // what is the problem we are solving
        // - understand which subexpressions of a consumer task map to its producer task(s)
        //   -- when tasks communicate in simple ways (e.g., a fully-serial pipeline), there is a trivial mapping between the pointers that come from producer and are eaten by consumer
        //   -- when tasks communicate in complicated ways (e.g., consumers of parallel producers), there isn't a trivial mapping between producer pointers and subexpressions in the consumer task
        //      --- e.g., 
        //                in0, in1 = randInit()
        //                in2, in3 = randInit()
        //                ptr0 = Task0(in0', in1') [in0' and in1' are aliases for their respective producer outputs, since they may exist in a different context from the producer and hence have no static mapping between each other]
        //                ptr1 = Task1(in2', in3', ptr0') [ptr0' is an alias for ptr0, which may be a value in a different context from ptr0 ie there is no explicit static mapping between ptr0 and ptr0']
        //          ---- in this example, it is not clear (from the template's perspective) which argument is ptr0 and which argument is in2 and in3
        //          ---- would this problem be helped by getting rid of context-switching issues in the static code? 
        //               ----- Perhaps... but then you're reliant on LLVM's ability to inline everything... which doesn't always happen
        //          ---- would this problem be helped by using the characteristics of the subexpression to compare to the characteristics of the producer expression?
        //               ----- Perhaps... but it wouldn't work generally. A task that eats two images from the same image-reading method will have two subexpressions with the same characteristics
        // why does this problem matter
        // - understanding the mapping between values from a producer eaten by a consumer are necessary to generate cohesive and correct pipestages within a pipeline
        //   -- ie we need to know which subexpressions within our consumer map to the output(s) of the producer in order to emulate the behavior of the original pipeline
        // - why is this non-obvious
        //   -- The consumer's functional expression is generated in a context with symbols that don't directly map to the symbols from the producer
        //   -- this creates a situation where the consumer's expression has no static mapping to the outputs of the producer
        //   -- a lack of mapping creates ambiguity in the consumer's expression when it contains subexpressions from multiple producers
        // how are we solving it
        // - acquire the memory footprints of each base pointer in the application
        //   -- this allows ptr0 and ptr0' to be mapped together via the memory they touch
        // - e.g., in2, in3, and ptr0' will all touch unique memory footprints that map them together
        // where does this break
        // - when memory patterns are irregular (then the memory footprint might not be detectable)
        //    -- e.g., hash table. The stride pattern will be irregular and break the memory-tuple merge algorithm (which supports contiguous and strided memory accesses)
        // - when memory footprints are moved, or copied, with llvm intrinsics - then the producer memory footprint may not have the same id as the consumer memory footprint
        //   -- e.g., when llvm transforms a loop that moves memory into an llvm.memmov intrinsic
        // corner cases
        // - when pointers in the consumer map to the same memory footprint
        //   -- then it doesn't matter which subexpression goes where in the expression - they are the same thing
        // - when separate memory footprints are contiguous
        //   -- then their dynamically-observed base pointers create a boundary between them

        // for now we are treating the raw base pointer allocations as the footprints
        // we map memory accessing instructions to the footprints they touch
        // key is instruction ID, value is a set of footprint IDs
        map<int64_t, set<int64_t>> inst2Footprint;
        for( const auto& inst : instToTuple )
        {
            for( auto t : inst.second )
            {
                // [BW] 2024-03-22
                // it turns out that each memory tuple "t" is overreaching by 1 byte (added to the offset)
                // to normalize for this, we subtract from its offset by one - this gives us the correct overlap behavior when comparing to base pointers
                t.offset = t.offset - 1;
                for( const auto& bp : basePointers )
                {
                    auto o = mem_tuple_overlap(bp, t);
                    if( o.base )
                    {
                        inst2Footprint[ inst.first ].insert(bp.base);
                    }
                }
            }
        }
        return inst2Footprint;
    }
} // namespace Cyclebite::Profile::Backend::Memory