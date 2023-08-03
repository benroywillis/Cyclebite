#pragma once
#include "Function.h"
#include "DataGraph.h"
#include "ReductionVariable.h"

namespace Cyclebite::Grammar
{
    class Task;
    std::set<std::shared_ptr<InductionVariable>> getInductionVariables(const std::shared_ptr<Task>& t);
    std::set<std::shared_ptr<ReductionVariable>> getReductionVariables(const std::shared_ptr<Task>& t, const std::set<std::shared_ptr<InductionVariable>>& vars);
    std::set<std::shared_ptr<BasePointer>> getBasePointers(const std::shared_ptr<Task>& t);
    std::set<std::shared_ptr<Collection>> getCollections(const std::shared_ptr<Task>& t, const std::set<std::shared_ptr<InductionVariable>>& IVs, const std::set<std::shared_ptr<BasePointer>>& BPs);
    void Process(const std::set<std::shared_ptr<Task>>& tasks);
} // namespace Cyclebite::Grammar