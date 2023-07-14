#pragma once
#include <ctime>
#include <cstdint>
#include <set>
#include <map>
#include <memory>
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Value.h"

namespace TraceAtlas::Profile::Backend::Memory
{
    // forward declarations for some structures
    class Epoch;
    class CodeSection;
    class Kernel;
    class NonKernel;
    struct UIDCompare;

    /// Timing information
    extern struct timespec start, end;

    /// Thresholds
    /// Minimum offset a memory tuple must have (in bytes) to be considered for memory prod/cons graph 
    constexpr uint32_t MIN_TUPLE_OFFSET  = 32;
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
} // namespace TraceAtlas::Memory::Backend