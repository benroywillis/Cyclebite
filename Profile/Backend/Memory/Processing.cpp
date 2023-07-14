#include "Processing.h"
#include "Epoch.h"
#include "Kernel.h"
#include <fstream>
#include <cstdlib>

using namespace std;

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
            bool min = false;
            for( const auto& r : instance->memoryData.rTuples )
            {
                if( r.offset > MIN_TUPLE_OFFSET )
                {
                    min = true;
                    break;
                }
            }
            if( !min )
            {
                instance->memoryData.rTuples.clear();
            }
            min = false;
            for( const auto& w : instance->memoryData.wTuples )
            {
                if( w.offset > MIN_TUPLE_OFFSET )
                {
                    min = true;
                }
            }
            if( !min )
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
                        if( overlap.offset > MIN_TUPLE_OFFSET )
                        {
                            changes = true;
                        }
                    }
                }
            }
        }
        return pair(unExplainedConsumers, changes);
    }

    map<uint64_t, pair<set<uint64_t>, set<uint64_t>>> GenerateTaskCommunication()
    {
        // maps a code instance ID to its RAW (first) and WAW (second) dependencies
        map<uint64_t, pair<set<uint64_t>, set<uint64_t>>> taskCommunication;
        // first check to see if CIFootprints is even the correct size
        if( epochs.size() < 2 )
        {
            spdlog::warn("No memory dependency information can be generated because there is only one code instance");
            return taskCommunication;
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
                    if( t.type == __TA_MemType::Memcpy )
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
                    }
                    // memset just writes to things, effectively making it the last writer of that data
                    // thus there is nothing to pass on to the consumer
                    // if a producer memset a memory region, the regular last-writer code will take care of this case
                }
                auto newTupleSet = removeExplainedProducers((*producer)->memoryData.wTuples, consumed);
                consumed = newTupleSet.first;
                if( newTupleSet.second )
                {
                    taskCommunication[instance->IID].first.insert((*producer)->IID);
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
                    taskCommunication[instance->IID].second.insert((*producer)->IID);
                }
                producer = next(producer);
            }
        }
        return taskCommunication;
    }
} // namespace Cyclebite::Profile::Backend::Memory