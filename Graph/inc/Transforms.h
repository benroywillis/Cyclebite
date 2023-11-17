//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include <llvm/Analysis/CallGraph.h>
#include <llvm/IR/BasicBlock.h>
#include <map>
#include <set>

namespace Cyclebite::Graph
{
    class GraphNode;
    class ControlNode;
    class VirtualNode;
    class CallGraphNode;
    class MLCycle;
    class Graph;
    class CallGraph;
    class ControlGraph;
    struct p_GNCompare;
    struct KCompare;

    /// Keeps track of all dead blocks in the input program
    extern std::set<const llvm::BasicBlock *> deadCode;

    void Checks(const ControlGraph &transformed, std::string step, bool segmentation = false);
    std::shared_ptr<GraphNode> BlockToNode(const Graph &graph, const llvm::BasicBlock *block, const std::map<std::vector<uint32_t>, uint64_t> &NIDMap);
    const llvm::BasicBlock *NodeToBlock(const std::shared_ptr<ControlNode> &node, const std::map<int64_t, const llvm::BasicBlock *> &IDToBlock);
    //std::set<std::shared_ptr<ControlNode> , p_GNCompare> ReduceMO(Graph& graph, int inputOrder, int desiredOrder);
    void reverseTransform(Graph &graph);
    ControlGraph reverseTransform_MLCycle(const ControlGraph& graph);
    bool hasIndirectRecursion(const Cyclebite::Graph::CallGraph &graph, const std::shared_ptr<Cyclebite::Graph::CallGraphNode> &node);
    bool hasIndirectRecursion(const llvm::CallGraphNode *node);
    bool hasDirectRecursion(const llvm::CallGraphNode *node);
    bool hasDirectRecursion(const Cyclebite::Graph::CallGraph &graph, const std::shared_ptr<Cyclebite::Graph::CallGraphNode> &src);
    void VirtualizeSubgraph(Graph &graph, std::shared_ptr<VirtualNode> &VN, const ControlGraph &subgraph);
    void VirtualizeSharedFunctions(ControlGraph &graph, const Cyclebite::Graph::CallGraph &dynamicCG);
    std::vector<std::shared_ptr<MLCycle>> VirtualizeKernels(std::set<std::shared_ptr<MLCycle>, KCompare> &newKernels, ControlGraph &graph);
    void SumToOne(const std::set<std::shared_ptr<GraphNode>, p_GNCompare> &nodes);
    ControlGraph TrivialTransforms(const std::shared_ptr<ControlNode> &sourceNode);
    ControlGraph BranchToSelectTransforms(const ControlGraph &graph, const std::shared_ptr<ControlNode> &source);
    bool FanInFanOutTransform(ControlGraph &subgraph, const std::shared_ptr<ControlNode> &source, const std::shared_ptr<ControlNode> &sink);
    const std::shared_ptr<ControlNode> FindNewSubgraph(ControlGraph &subgraph, const std::shared_ptr<ControlNode> &source);
    //bool MergeForks(Graph &subgraph, const std::shared_ptr<ControlNode> &source, const std::shared_ptr<ControlNode> &sink);
    void ApplyCFGTransforms(ControlGraph &graph, const Cyclebite::Graph::CallGraph &dynamicCG, bool segmentation = false);
    std::set<std::shared_ptr<MLCycle>, KCompare> FindMLCycles(ControlGraph &graph, const Cyclebite::Graph::CallGraph &dynamicCG, bool applyTransforms = false);
    void FindAllRecursiveFunctions(const llvm::CallGraph &CG, const Graph &graph, const std::map<int64_t, const llvm::BasicBlock *> &IDToBlock);
    void FindAllRecursiveFunctions(const Cyclebite::Graph::CallGraph &CG, const Graph &graph, const std::map<int64_t, const llvm::BasicBlock *> &IDToBlock);
} // namespace Cyclebite::Graph