//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include <cstdint>
#include <set>
#include <spdlog/spdlog.h>

namespace Cyclebite::Profile::Backend::Memory
{
    /// Describes the memory operation that took place
    enum class __TA_MemType
    {
        // lowest priority
        None,
        Reader,
        Writer,
        // middle priority
        Malloc,
        Free,
        // highest priority
        Memset,
        Memmov,
        Memcpy
    };

    /// Describes the temporal access patterns of the tuple
    /// For example, when determining whether an internal working set aliasing problem is occurring (thus creating false dependencies between modular operators), write-first-then-read makes a distinction from read-first-then-write (which is common among operators that work in-place)
    enum class __TA_TemporalAccess
    {
        // when the tuple is only read from/written to
        NA,
        // describes static working sets for shared operators
        WriteThenRead,
        // describes operators that work in-place
        ReadThenWrite,
        // when access patterns have no pattern
        Random
    };

    /// Memory access range that has been observed from
    struct MemTuple
    {
        /// Type of access that this tuple represents
        __TA_MemType type;
        /// Describes the access patterns of this tuple
        __TA_TemporalAccess AP;
        /// start address of the memory range
        /// the byte that starts at this address is "owned" by this tuple
        /// will always be 0 if this tuple is not valid
        uint64_t base;
        /// offset in bytes
        /// can be 0 if the tuple only owns a single byte
        uint32_t offset;
        /// Number of times this address range has been touched
        uint32_t refCount;
        MemTuple() : type(__TA_MemType::None), AP(__TA_TemporalAccess::NA), base(0), offset(0), refCount(0) {}
    };

    struct MTCompare
    {
        // in the rb tree, we want non-equal entries to be sorted by base address
        // if there is overlap, we want these entries to be "equal"
        // that way, no entries in the tree are allowed to overlap, and the left child of each node will contain the tuple with the "lesser" addresses than the right child
        // NOTE ON STL SETS
        // if lhs < rhs is false and lhs > rhs is false, the entries are considered equivalent
        bool operator()(const MemTuple &lhs, const MemTuple &rhs) const
        {
            if( lhs.base < rhs.base )
            {
                if( lhs.base+(uint64_t)lhs.offset < rhs.base )
                {
                    // return lhs.base < rhs.base is true
                    return true;
                }
                else
                {
                    // these tuples overlap, therefore they are equivalent
                    return false;
                }
            }
            else
            {
                // there are two cases here
                // first case, rhs.base <= lhs.base, which should return false because that is the wrong sorted order
                // second, rhs and lhs overlap, which should also return false
                // therefore we just return false
                return false;
            }
        }
    };

    struct MemTypeCompare
    {
        bool operator()(const __TA_MemType& lhs, const __TA_MemType& rhs) const
        {
            // memory operators (memset, memmov, memcpy) get highest priority
            if( lhs >= __TA_MemType::Memset )
            {
                if( rhs >= __TA_MemType::Memset )
                {
                    // lhs === rhs
                    return false;
                }
                else
                {
                    // lhs > rhs
                    return true;
                }
            }
            // dynamic memory management (malloc, free) is in the middle
            else if( lhs >= __TA_MemType::Malloc )
            {
                if( rhs >= __TA_MemType::Malloc )
                {
                    // lhs === rhs or lhs < rhs
                    return false;
                }
                else
                {
                    // lhs > rhs
                    return true;
                }
            }
            // vanilla memory ops (reader, writer) get lowest priority
            else
            {
                // lhs can't possibly be greater than rhs
                return false;
            }
        }
    };

    struct MemAccessCompare
    {
        bool operator()(const __TA_TemporalAccess& lhs, const __TA_TemporalAccess& rhs) const
        {
            // To decide the tuple access pattern, we prioritize specific access patterns from NA or random
            // Priority (in decending order)
            // - ReadThenWrite, WriteThenRead
            // - Random, NA
            if( lhs == __TA_TemporalAccess::ReadThenWrite || lhs == __TA_TemporalAccess::WriteThenRead )
            {
                // two cases
                // - we have a tie, arbitrarily pick the lhs
                // - lhs has the higher priority, so pick lhs
                // either way we pick lhs to be higher priority
                return true;
            }
            else
            {
                if( rhs == __TA_TemporalAccess::ReadThenWrite || rhs == __TA_TemporalAccess::WriteThenRead )
                {
                    // rhs has the higher priority
                    return false;
                }
                else
                {
                    // it is a tie, lhs is picked arbitrarily to be greater
                    return true;
                }
            }
        }
    };

    /// @brief Merges all members of two memory tuples
    ///
    /// This method assumes that the memory tuple ranges already overlap, and that they are the same operation.
    /// The refcounts are summed and incremented.
    /// Effectively this is a union operator on two memory tuples
    /// @param lhs  One memory tuple. The type of this memory tuple is used to make the new tuple
    /// @param rhs  The other tuple. 
    /// @retval     A new memory tuple with matching operation, merged memory range and accumulatd refcount
    inline MemTuple merge_tuples(const MemTuple& lhs, const MemTuple& rhs)
    {
        MemTuple newTuple;
        // this logic finds the leftmost (least) address, which is trivially the lesser base
        if( lhs.base < rhs.base )
        {
            newTuple.base = lhs.base;
        }
        else
        {
            newTuple.base = rhs.base;
        }
        // this logic finds the rightmost (greatest) address, and does some math to make sure base+offset is equal to that address
        if( (lhs.base+lhs.offset) > (rhs.base+rhs.offset) )
        {
            newTuple.offset = (uint32_t)(lhs.base+(uint64_t)lhs.offset - newTuple.base);
        }
        else
        {
            newTuple.offset = (uint32_t)(rhs.base+(uint64_t)rhs.offset - newTuple.base);
        }
        newTuple.refCount = lhs.refCount + rhs.refCount + 1;
        
        // To decide the type, there is priority. 
        // The libc memory operators (memset, memmov, memcpy) get highest priority
        // Next are the memory allocators (malloc, free)
        // Last are the memory operators (Reader, Writer)
        MemTypeCompare mtc;
        if( mtc(lhs.type, rhs.type) )
        {
            newTuple.type = lhs.type;
        }
        else if( !mtc(lhs.type, rhs.type) && !mtc(rhs.type, lhs.type) )
        {
            newTuple.type = lhs.type;
        }
        else
        {
            newTuple.type = rhs.type;
        }
        // pick the higher-priority temporal access pattern
        MemAccessCompare mac;
        if( mac(lhs.AP, rhs.AP) )
        {
            newTuple.AP = lhs.AP;
        }
        else
        {
            newTuple.AP = rhs.AP;
        }
        return newTuple;
    }

    /// @brief Return the overlapping region of memory shared by the two arguments
    ///
    /// This is an intersection of two memory tuples.
    /// The only struct members considered in this operation are base and offset. Thus this operation doesn't care what memory operation either of the tuple arguments have.
    /// @param lhs  One tuple to intersect
    /// @param rhs  The other tuple to intersect
    /// @retval A tuple that represents the overlap of the memory range of the two memory overlaps. The only two valid tuple members are base and offset. If there is no overlap, both will be 0.
    inline MemTuple mem_tuple_overlap(const MemTuple& lhs, const MemTuple& rhs)
    {
        MemTuple overlap;
        overlap.base = 0;
        overlap.offset = 0;
        // if the rhs base starts within the lhs range, there is overlap
        if( (lhs.base <= rhs.base) && (rhs.base <= (lhs.base+(uint64_t)lhs.offset)) )
        {
            overlap.base   = rhs.base;
        }
        // if the lhs base starts within the rhs range, there is overlap
        else if( (rhs.base <= lhs.base) && (lhs.base <= (rhs.base+(uint64_t)rhs.offset)) )
        {
            overlap.base   = lhs.base;
        }
        if( overlap.base )
        {
            // find where the overlap ends
            // this should be at the least-ending address
            // and thus the overlap is (least ending address) - (greatest starting address [overlap.base])
            overlap.offset = (lhs.base+(uint64_t)lhs.offset) < (rhs.base+(uint64_t)rhs.offset) ? (uint32_t)((lhs.base+(uint64_t)lhs.offset) - overlap.base) : (uint32_t)((rhs.base+(uint64_t)rhs.offset) - overlap.base);
        }
        return overlap;
    }

    /// @brief Returns the memory range in lhs that is exclusive from rhs
    ///
    /// This is equivalent to lhs - intersect(lhs, rhs)
    /// @param lhs  Memory tuple to subtract from
    /// @param rhs  Memory tuple to take away from lhs
    /// @retval     The region of memory that is exclusive to lhs
    inline std::vector<MemTuple> mem_tuple_exclusion(const MemTuple& lhs, const MemTuple& rhs)
    {
        // vector is needed in case we split lhs
        std::vector<MemTuple> exclusiveRegions;
        // memory on the lhs of the number line that is exclusive to lhs
        MemTuple exclusiveLhs = lhs;
        // memory on the rhs of the number line that is exclusive to lhs
        MemTuple exclusiveRhs = rhs;

        auto intersect = mem_tuple_overlap(lhs, rhs);
        // if there is overlap
        if( intersect.base+(uint64_t)intersect.offset > 0 )
        {
            // check for complete overlap
            if( (intersect.base <= lhs.base) && ((lhs.base+(uint64_t)lhs.offset) <= (intersect.base+(uint64_t)intersect.offset)) )
            {
                // there was complete overlap, so return an empty exclusiveRegion vector
            }
            else
            {
                // we know there is overlap and we know there isn't complete coverage. Thus we have three cases: 
                // - lhs.base is outside of the intersection and does not extend beyond the intersect
                // - lhs.base is outside the intersect and extends beyond the intersect
                // - lhs.base is within the intersection (and we already know it must extend beyond the intersect because the above condition rules out a complete overlap of lhs and intersect)
                // in the first case, the exclusive region will start at lhs.base and end at lhs.base+(intersect.base-lhs.base-1)
                // in the second case, the exclusive region will be split. One region will be case 1, the other region will be case 3
                // in the third case, the exclusive region will start at intersect.base+intersect.offset+1 and end at lhs.base+lhs.offset
                
                // case 1
                // if the lhs tuple starts before the intersect
                if( lhs.base < intersect.base )
                {
                    // then exclusiveLhs starts at lhs
                    exclusiveLhs.base   = lhs.base;
                    // and its offset goes to the start of the intersect
                    exclusiveLhs.offset = (uint32_t)(intersect.base - exclusiveLhs.base - 1);
                    exclusiveRegions.push_back(exclusiveLhs);
                }
                // case 3
                // if lhs.base is within intersect
                if( (intersect.base <= lhs.base) && (lhs.base <= (intersect.base+(uint64_t)intersect.offset)) )
                {
                    // then exclusiveRhs starts at the end of the intersection
                    exclusiveRhs.base   = intersect.base+(uint64_t)intersect.offset+1;
                    // and the offset goes to the end of lhs
                    exclusiveRhs.offset = (uint32_t)(lhs.base+(uint64_t)lhs.offset-exclusiveRhs.base);
                    exclusiveRegions.push_back(exclusiveRhs);
                }
                // case 2 has been solved if both case 1 and case 3 are true
#ifdef DEBUG
                if( exclusiveRegions.empty() )
                {
                    spdlog::critical("Could not find an exclusive region between memory tuples when there should be!");
                    exit(EXIT_FAILURE);
                }
#endif
            }
        }
        else
        {
            // there was no intersection, so we just return lhs
            exclusiveRegions.push_back(lhs);
        }
        return exclusiveRegions;
    }

    /// @brief This is a tail-recursive algorithm to merge a new tuple into an array of existing tuples
    ///
    /// When many tuples exist in an array, it is possible for a new tuple entry to connect a large number of them at once
    /// This method solves that problem by merging tuples until it doesn't find a conflict in the array anymore
    /// @param  array   Set of MemTuple structures to push a new tuple into. This array should have entries whose operation are all the same (ie exclusively readers and writers)
    /// @param  tuple   New tuple to push into the set
    inline void merge_tuple_set(std::set<MemTuple, MTCompare>& array, const MemTuple& tuple)
    {
        // the MTCompare function only returns true if there is OVERLAP between tuples - however, when we combine tuples, we want to find CONTIGUOUS tuples as well
        // this means that we need to expand the range of the input tuple by one in each direction such that contiguous and overlapping tuples are combined
        MemTuple searchTuple = tuple;
        searchTuple.base = searchTuple.base - 1;
        searchTuple.offset = searchTuple.offset + 1;
        // this algorithm needs to be recursive
        // getting one memory entry that unifies multiple entries in the set will require recursion to satisfy
        auto match = array.find(searchTuple);
        auto newTuple = tuple;
        while( match != array.end() )
        {
            // combine the existing tuple and re-enter it
            auto existingTuple = *match;
            array.erase(existingTuple);
            // the existing tuple contains observations that we have made in past time, thus we determine temporal access patterns here
            if( existingTuple.AP == __TA_TemporalAccess::NA )
            {
                if( existingTuple.type == __TA_MemType::Reader || existingTuple.type == __TA_MemType::Memcpy )
                {
                    if( tuple.type == __TA_MemType::Writer || tuple.type == __TA_MemType::Memset )
                    {
                        existingTuple.AP = __TA_TemporalAccess::ReadThenWrite;
                    }
                }
                else if( existingTuple.type == __TA_MemType::Writer || existingTuple.type == __TA_MemType::Memset )
                {
                    if( tuple.type == __TA_MemType::Reader || tuple.type == __TA_MemType::Memcpy )
                    {
                        existingTuple.AP = __TA_TemporalAccess::WriteThenRead;
                    }
                }
                // no case yet for __TA_TemporalAccess::Random
            }
            newTuple = merge_tuples(existingTuple, newTuple);
            searchTuple = newTuple;
            searchTuple.base = searchTuple.base - 1;
            searchTuple.offset = searchTuple.offset + 1;
            match = array.find(searchTuple);
        }
        auto it = array.insert(newTuple);
#ifdef DEBUG
        if( it.second == false )
        {
            spdlog::critical("Tuple merge did not push its final tuple!");
            exit(EXIT_FAILURE);
        }
#endif
    }

    /// @brief Removes the memory ranges in "tuple" that may be present in "array"
    /// 
    /// @param array    Set of MemTuples to remove from
    /// @param tuple    Memory range to remove from "array"
    inline void remove_tuple_set(std::set<MemTuple, MTCompare>& array, const MemTuple& tuple)
    {
        auto match = array.find(tuple);
        while( match != array.end() )
        {
            // split tuples and re-enter the split
            auto existingTuple = *match;
            array.erase(existingTuple);
            auto newTuples = mem_tuple_exclusion(existingTuple, tuple);
            for( const auto& newT : newTuples )
            {
                array.insert(newT);
            }
            match = array.find(tuple);
        }
    }
}