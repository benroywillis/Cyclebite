//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include "Memory.h"

namespace Cyclebite::Profile::Backend::Memory
{
    /// @return { EpochID: ({EpochIDs the key has a RAW with}, {EpochIDs the key has a WAW with}) }
    extern std::map<uint64_t, std::pair<std::set<uint64_t>, std::set<uint64_t>>> TaskCommunications;
    /// @brief Parses the entrances and exits of kernels to decide which edges in the graph represent task boundaries
    ///
    /// An epoch is a time interval of the program that is taken by a task instance. A task instance can be a single kernel or kernel hierarchy.
    /// An epoch boundary is a state transition in the program where we can definitively say that a task has been entered or exited
    /// A boundary must satisfy the following rules
    /// 1. Each side of the boundary (the source and sink node) must not be an intersection of the kernel block-nonkernel block sets ie they cannot both be a kernel and nonkernel
    void FindEpochBoundaries();
    void GenerateMemoryRegions();
    void ProcessEpochBoundaries();
    /// @brief Implements the algorithm to discover communication between epochs
    /// @return Populates the TaskCommunications global
    void GenerateTaskCommunication();
    std::map<int64_t, std::set<int64_t>> CombineStridedTuples();
} // namespace Cyclebite::Profile::Backend::Memory