//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include <ctime>
#include <cstdint>
#include <set>
#include <map>
#include <memory>
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Value.h"

namespace Cyclebite::Profile::Backend::Memory
{
    // forward declarations for some structures
    class Epoch;
    class CodeSection;
    class Kernel;
    class NonKernel;
    struct UIDCompare;
    struct MemTuple;
    struct MTCompare;

    /// Timing information
    extern struct timespec start, end;

    /// Thresholds
    /// Minimum offset a memory tuple must have (in bytes) to be considered for memory prod/cons graph 
    constexpr uint32_t MIN_MEMORY_OFFSET = 128;
    /// Minimum acceptable frequency for a kernel instance
    constexpr uint64_t MIN_EPOCH_FREQ    = 32;
    /// Minimum block overlap for an epoch to match a kernel
    constexpr float EPOCH_KERNEL_OVERLAP = 0.5f;

    /// Maps critical edges to the codesections they enter
    /// Holds edges that transition from one part of the program to the other
    /// These edges are encoded as pairs of block IDs, first entry is the source, second is sink
    extern std::map<std::pair<int64_t, int64_t>, std::set<std::shared_ptr<CodeSection>, UIDCompare>> epochBoundaries;
    /// holds all epochs that have been observed
    extern std::set<std::shared_ptr<Epoch>, UIDCompare> epochs;
    /// holds all sets of basic blocks that should be observed in an epoch at some point in the profile
    extern std::map<uint64_t, std::set<int64_t>> taskCandidates;
    /// Maps instructions to their working set tuples
    /// These mappings are used in the grammar tool to figure out which load instructions are touching critical pieces of memory
    extern std::map<int64_t, std::set<MemTuple, MTCompare>> instToTuple;
    /// Keeps track of base pointers as they are profiled
    /// Base pointers are used as boundaries between memory footprints when memory tuples are combined (more aggressively) after processing
    /// First of the pair is the base pointer address, second is the size of the allocation
    extern std::set<MemTuple, MTCompare> basePointers;
    /// This map points from base pointers to their moved or copied footprints
    /// The map uses the base of each base pointer as their ID (that's the int64_t)
    /// Key-value pairs represent moves or copies of a memory footprint to another - bp's can have this done multiple times (hence the set of values a bp may map to)
    extern std::map<int64_t, std::set<int64_t>> bp2bp;
    /// Holds all CodeSections
    /// A code section is a unique set of basic block IDs ie a codesection may map to multiple kernels
    extern std::set<std::shared_ptr<CodeSection>, UIDCompare> codeSections;
    /// Holds all unique block sets in the program
    extern std::set<std::shared_ptr<Kernel>, UIDCompare> blockSets;

    extern std::set<std::shared_ptr<Kernel>, UIDCompare> kernels;
    /// A set of block IDs that have already executed been seen in the profile
    extern std::set<int64_t> executedBlocks;
    /// Holds the current kernel instance(s)
    extern std::shared_ptr<Epoch> currentEpoch;
    /// Remembers the block seen before the current so we can dynamically find kernel exits
    extern int64_t lastBlock;
    /// On/off switch for the profiler
    extern bool memoryActive;
} // namespace Cyclebite::Memory::Backend