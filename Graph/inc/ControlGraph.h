#pragma once
#include "ControlNode.h"
#include "Graph.h"

namespace TraceAtlas::Graph
{
    class ControlGraph : public Graph
    {
    public:
        ControlGraph();
        ControlGraph(const std::set<std::shared_ptr<ControlNode>, p_GNCompare> &nodeSet, const std::set<std::shared_ptr<UnconditionalEdge>, GECompare> &edgeSet, const std::shared_ptr<ControlNode>& terminator);
        // upgrade constructor
        ControlGraph(const Graph& graph, const std::shared_ptr<ControlNode>& terminator);
        const std::shared_ptr<ControlNode> getNode(const std::shared_ptr<ControlNode> &s) const;
        const std::shared_ptr<ControlNode> getNode(uint64_t ID) const;
        const std::shared_ptr<ControlNode> getFirstNode() const;
        /// Returns the node whose underlying block terminated the program
        const std::shared_ptr<ControlNode> getProgramTerminator() const;
        /// Returns all nodes whose underlying blocks terminated a thread
        /// This includes the terminator block of the program and the terminator blocks of any threads
        const std::set<std::shared_ptr<ControlNode>, p_GNCompare> getAllTerminators() const;
        const std::set<std::shared_ptr<ControlNode>, p_GNCompare> getControlNodes() const;
        const std::set<std::shared_ptr<UnconditionalEdge>, GECompare> getControlEdges() const;
    private:
        // node that was observed to terminate the program
        std::shared_ptr<ControlNode> programTerminator;
    };
} // namespace TraceAtlas::Graph