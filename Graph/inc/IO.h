//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include <llvm/Analysis/CallGraph.h>
#include <llvm/IR/BasicBlock.h>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <string>

namespace Cyclebite::Graph
{
    class ControlNode;
    class DataValue;
    class Inst;
    class ControlBlock;
    class MLCycle;
    class UnconditionalEdge;
    class CallEdge;
    class Graph;
    class ControlGraph;
    class DataGraph;
    class CallGraph;
    struct GNCompare;
    struct p_GNCompare;
    struct KCompare;
    struct GECompare;
    // maps a block ID list (that represents the markov chain state, which could have arbitrary order number) to an llvm::BasicBlock ID which the node represents
    // instantiated in cartographer/new/IO.cpp
    extern std::map<std::vector<uint32_t>, uint64_t> NIDMap;
    // maps an llvm instruction to its corresponding libGraph datanode, initialized in Graph/IO.cpp:BuildDFG()
    extern std::map<const llvm::Value*, const std::shared_ptr<DataValue>> DNIDMap;
    // maps an llvm basic block to its corresponding libGraph controlnode, initialized in Graph/IO.cpp:BuildDFG()
    extern std::map<const llvm::BasicBlock*, const std::shared_ptr<ControlBlock>> BBCBMap;
    struct EntropyInfo
    {
        double start_entropy_rate;
        double start_total_entropy;
        uint32_t start_node_count;
        uint32_t start_edge_count;
        double end_entropy_rate;
        double end_total_entropy;
        uint32_t end_node_count;
        uint32_t end_edge_count;
    };
    double TotalEntropy(const std::set<std::shared_ptr<ControlNode>, p_GNCompare> &nodes);
    double EntropyCalculation(const std::set<std::shared_ptr<ControlNode>, p_GNCompare> &nodes);
    void getDynamicInformation(Cyclebite::Graph::ControlGraph& cg, Cyclebite::Graph::CallGraph& dynamicCG, const std::string& filePath, const std::unique_ptr<llvm::Module>& SourceBitcode, const llvm::CallGraph& staticCG, const std::map<int64_t, std::vector<int64_t>>& blockCallers, const std::set<int64_t>& threadStarts, const std::map<int64_t, llvm::BasicBlock*>& IDToBlock, bool HotCodeDetection);
    int BuildCFG(Graph &graph, const std::string &filename, bool HotCodeDetection);
    const Cyclebite::Graph::CallGraph getDynamicCallGraph(llvm::Module *mod, const Graph &graph, const std::map<int64_t, std::vector<int64_t>> &blockCallers, const std::map<int64_t, llvm::BasicBlock *> &IDToBlock);
    void CallGraphChecks(const llvm::CallGraph &SCG, const Cyclebite::Graph::CallGraph &DCG, const Graph &dynamicGraph, const std::map<int64_t, llvm::BasicBlock *> &IDToBlock);
    int BuildDFG(llvm::Module *SourceBitcode, const Cyclebite::Graph::CallGraph& dynamicCG, std::map<int64_t, std::shared_ptr<ControlNode>> &blockToNode, std::set<std::shared_ptr<ControlBlock>, p_GNCompare> &programFlow, DataGraph &graph, std::map<std::string, std::set<int64_t>> &specialInstructions, const std::map<int64_t, llvm::BasicBlock*>& IDToBlock);
    std::map<std::string, std::map<std::string, std::map<std::string, int>>> ProfileKernels(const std::map<std::string, std::set<int64_t>> &kernels, llvm::Module *M, const std::map<int64_t, uint64_t> &blockCounts);
    std::set<std::pair<int64_t, int64_t>> findOriginalBlockIDs(const std::shared_ptr<UnconditionalEdge>& edge);
    void WriteKernelFile(const ControlGraph &graph, const std::set<std::shared_ptr<MLCycle>, KCompare> &kernels, const std::map<int64_t, llvm::BasicBlock *> &IDToBlock, const std::map<int64_t, std::vector<int64_t>> &blockCallers, const EntropyInfo &info, const std::string &OutputFileName, bool hotCode = false);
    std::string GenerateDot(const Graph &graph, bool original = false);
    std::string GenerateCoverageDot(const std::set<std::shared_ptr<ControlNode>, p_GNCompare> &coveredNodes, const std::set<std::shared_ptr<ControlNode>, p_GNCompare> &uncoveredNodes);
    std::string GenerateTransformedSegmentedDot(const std::set<std::shared_ptr<ControlNode>, p_GNCompare> &nodes, const std::set<std::shared_ptr<MLCycle>, KCompare> &kernels, int markovOrder);
    void GenerateDynamicCoverage(const std::set<std::shared_ptr<ControlNode>, p_GNCompare> &dynamicNodes, const std::set<std::shared_ptr<ControlNode>, p_GNCompare> &staticNodes);
    ControlGraph GenerateStaticCFG(llvm::Module *M);
    std::string GenerateDataDot(const std::set<std::shared_ptr<DataValue>, p_GNCompare> &nodes);
    std::string GenerateBBSubgraphDot(const std::set<std::shared_ptr<ControlBlock>, p_GNCompare> &BBs);
    std::string GenerateHighlightedSubgraph(const Graph &graph, const Graph &subgraph);
    std::string GenerateFunctionSubgraph(const Graph &funcGraph, const std::shared_ptr<CallEdge> &entrance);
    std::string GenerateCallGraph(const llvm::CallGraph &CG);
    std::string GenerateCallGraph(const Cyclebite::Graph::CallGraph &CG);
} // namespace Cyclebite::Graph