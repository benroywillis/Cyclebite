#pragma once
#include "Graph/inc/Inst.h"
#include "Graph/inc/DataGraph.h"
#include <nlohmann/json.hpp>
#include "Inst.h"

namespace Cyclebite::Grammar
{
    // contains all datanodes (loads and stores) that touches significant memory
    extern std::set<std::shared_ptr<Cyclebite::Graph::Inst>, Cyclebite::Graph::p_GNCompare> SignificantMemInst;
    void InjectSignificantMemoryInstructions(const nlohmann::json& instanceJson, const std::map<int64_t, llvm::Value*>& IDToValue);
} // namespace Cyclebite::Grammar