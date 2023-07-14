#pragma once
#include "MemoryTuple.hpp"

namespace Cyclebite::Profile::Backend::Memory
{
    class Iteration
    {
    public:
        /// Memory tuples whose operation was to read from the specified addresses
        std::set<MemTuple, MTCompare> rTuples;
        /// Memory tuples whose operation was to write to the specified addresses
        std::set<MemTuple, MTCompare> wTuples;
        /// The time in which this iteration occurred in iteration time
        uint64_t time;
        Iteration();
        /// Clears the sets and sets time to 0
        void clear();
    };
}