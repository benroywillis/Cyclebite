#pragma once
#include <llvm/Analysis/CallGraph.h>
#include <llvm/IR/BasicBlock.h>
#include <map>
#include <set>

namespace TraceAtlas::Graph
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
    llvm::BasicBlock *NodeToBlock(const std::shared_ptr<ControlNode> &node, const std::map<int64_t, llvm::BasicBlock *> &IDToBlock);
    //std::set<std::shared_ptr<ControlNode> , p_GNCompare> ReduceMO(Graph& graph, int inputOrder, int desiredOrder);
    void reverseTransform(Graph &graph);
    ControlGraph reverseTransform_MLCycle(const ControlGraph& graph);
    bool hasIndirectRecursion(const TraceAtlas::Graph::CallGraph &graph, const std::shared_ptr<TraceAtlas::Graph::CallGraphNode> &node);
    bool hasIndirectRecursion(const llvm::CallGraphNode *node);
    bool hasDirectRecursion(const llvm::CallGraphNode *node);
    bool hasDirectRecursion(const TraceAtlas::Graph::CallGraph &graph, const std::shared_ptr<TraceAtlas::Graph::CallGraphNode> &src);
    void VirtualizeSubgraph(Graph &graph, std::shared_ptr<VirtualNode> &VN, const ControlGraph &subgraph);
    void VirtualizeSharedFunctions(ControlGraph &graph, const TraceAtlas::Graph::CallGraph &dynamicCG);
    std::vector<std::shared_ptr<MLCycle>> VirtualizeKernels(std::set<std::shared_ptr<MLCycle>, KCompare> &newKernels, ControlGraph &graph);
    void SumToOne(const std::set<std::shared_ptr<GraphNode>, p_GNCompare> &nodes);
    ControlGraph TrivialTransforms(const std::shared_ptr<ControlNode> &sourceNode);
    ControlGraph BranchToSelectTransforms(const ControlGraph &graph, const std::shared_ptr<ControlNode> &source);
    bool FanInFanOutTransform(ControlGraph &subgraph, const std::shared_ptr<ControlNode> &source, const std::shared_ptr<ControlNode> &sink);
    const std::shared_ptr<ControlNode> FindNewSubgraph(ControlGraph &subgraph, const std::shared_ptr<ControlNode> &source);
    //bool MergeForks(Graph &subgraph, const std::shared_ptr<ControlNode> &source, const std::shared_ptr<ControlNode> &sink);
    void ApplyCFGTransforms(ControlGraph &graph, const TraceAtlas::Graph::CallGraph &dynamicCG, bool segmentation = false);
    std::set<std::shared_ptr<MLCycle>, KCompare> FindMLCycles(ControlGraph &graph, const TraceAtlas::Graph::CallGraph &dynamicCG, bool applyTransforms = false);
    void FindAllRecursiveFunctions(const llvm::CallGraph &CG, const Graph &graph, const std::map<int64_t, llvm::BasicBlock *> &IDToBlock);
    void FindAllRecursiveFunctions(const TraceAtlas::Graph::CallGraph &CG, const Graph &graph, const std::map<int64_t, llvm::BasicBlock *> &IDToBlock);
} // namespace TraceAtlas::Graph