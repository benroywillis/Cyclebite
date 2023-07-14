#pragma once
#include "CallGraphEdge.h"
#include "CallGraphNode.h"
#include "Graph.h"

namespace TraceAtlas::Graph
{
    class CallGraph : public Graph
    {
    public:
        std::set<std::shared_ptr<CallGraphNode>, CGNCompare> CGN;
        CallGraph();
        CallGraph(const std::set<std::shared_ptr<CallGraphNode>, CGNCompare> &nodeSet, const std::set<std::shared_ptr<CallGraphEdge>, GECompare> &edgeSet);
        const std::set<std::shared_ptr<CallGraphNode>, CGNCompare> getCallNodes() const;
        bool find(const llvm::Function *f) const;
        const std::shared_ptr<CallGraphNode> &operator[](const llvm::Function *f) const;
        void addNode(const std::shared_ptr<CallGraphNode> &a);
        void addNodes(const std::set<std::shared_ptr<CallGraphNode>, CGNCompare> &nodes);
        const std::shared_ptr<CallGraphNode> getMainNode() const;
    };
} // namespace TraceAtlas::Graph