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
    class Task;
    struct TaskIDCompare;
    class Cycle;
    class IndexVariable;
    class Collection;
    /// @brief Maps file names to their lines of source code
    extern std::map<std::string, std::vector<std::string>> fileLines;
    extern std::map<uint32_t, std::pair<std::string,uint32_t>> blockToSource;
    // contains all datanodes (loads and stores) that touches significant memory
    extern std::set<std::shared_ptr<Cyclebite::Graph::Inst>, Cyclebite::Graph::p_GNCompare> SignificantMemInst;
    void InitSourceMaps(const std::unique_ptr<llvm::Module>& SourceBitcode);
    void InjectSignificantMemoryInstructions(const nlohmann::json& instanceJson, const std::map<int64_t, const llvm::Value*>& IDToValue);
    std::string PrintIdxVarTree( const std::set<std::shared_ptr<IndexVariable>>& idxVars );
    std::string VisualizeCollection( const std::shared_ptr<Collection>& coll );
    void OMPAnnotateSource( const std::set<std::shared_ptr<Cycle>>& parallelSpots, const std::set<std::shared_ptr<Cycle>>& vectorSpots );
    // print tasks with specialInstructions highlighted
    void PrintDFGs(const std::set<std::shared_ptr<Task>, TaskIDCompare>& tasks);
    void OutputJson( const std::unique_ptr<llvm::Module>& SourceBitcode, const std::set<std::shared_ptr<Task>, TaskIDCompare>& tasks, std::string OutputFile );
} // namespace Cyclebite::Grammar