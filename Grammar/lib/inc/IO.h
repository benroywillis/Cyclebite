//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include "Graph/inc/Inst.h"
#include "Graph/inc/DataGraph.h"
#include <nlohmann/json.hpp>

namespace Cyclebite::Grammar
{
    class IndexVariable;
    class Collection;
    // contains all datanodes (loads and stores) that touches significant memory
    extern std::set<std::shared_ptr<Cyclebite::Graph::Inst>, Cyclebite::Graph::p_GNCompare> SignificantMemInst;
    void InjectSignificantMemoryInstructions(const nlohmann::json& instanceJson, const std::map<int64_t, llvm::Value*>& IDToValue);
    std::string PrintIdxVarTree( const std::set<std::shared_ptr<IndexVariable>>& idxVars );
    std::string VisualizeCollection( const std::shared_ptr<Collection>& coll );
} // namespace Cyclebite::Grammar