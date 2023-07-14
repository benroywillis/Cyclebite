#pragma once
#include "Function.h"
#include "DataGraph.h"
#include "ReductionVariable.h"

namespace TraceAtlas::Grammar
{
    class Task;
    /// @brief Sets the threshold, in bytes, that a memory allocation must make in order to be considered a base pointer
    constexpr uint64_t ALLOC_THRESHOLD = 128;

    std::set<std::shared_ptr<InductionVariable>> getInductionVariables(const std::shared_ptr<Task>& t);
    std::set<std::shared_ptr<ReductionVariable>> getReductionVariables(const std::shared_ptr<Task>& t, const std::set<std::shared_ptr<InductionVariable>>& vars);
    std::set<std::shared_ptr<BasePointer>> getBasePointers(const std::shared_ptr<Task>& t);
    std::set<std::shared_ptr<Collection>> getCollections(const std::shared_ptr<Task>& t, const std::set<std::shared_ptr<InductionVariable>>& IVs, const std::set<std::shared_ptr<BasePointer>>& BPs);
    void Process(const std::set<std::shared_ptr<Task>>& tasks);
} // namespace TraceAtlas::Grammar