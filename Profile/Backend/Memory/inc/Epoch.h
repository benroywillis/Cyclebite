//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include "UniqueID.h"
#include "Iteration.h"
#include <set>
#include <map>

namespace Cyclebite::Profile::Backend::Memory
{
    class Kernel;
    /// Holds all information we care about in an epoch
    class Epoch : public UniqueID
    {
    public:
        // code section information
        std::set<int64_t> blocks;
        std::map<int64_t, std::set<int64_t>> entrances;
        std::map<int64_t, std::set<int64_t>> exits;
        // instance information
        Iteration memoryData;
        std::map<int64_t, uint64_t> freq;
        std::shared_ptr<Kernel> kernel;
        std::set<MemTuple, MTCompare> malloc_ptrs;
        std::set<int64_t> free_ptrs;
        Epoch();
        void updateBlocks(int64_t id);
        uint64_t getMaxFreq();
    };
}