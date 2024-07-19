//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include <cstdint>
#include <memory>
#include <set>
#include <vector>

namespace Cyclebite::Graph
{
    class Graph;
    class GraphNode;
    struct p_GNCompare;
    bool FindCycles(const Graph &graph);
    std::vector<std::set<std::shared_ptr<GraphNode>>> FindAllUniqueCycles(const std::set<std::shared_ptr<GraphNode>, p_GNCompare> &subgraph);

    enum class NodeColor
    {
        White,
        Grey,
        Black
    };
    /// Holds the transformation from a Cyclebite::Graph::ControlNode to a negative-log-space node
    struct DijkstraNode
    {
        DijkstraNode() = default;
        DijkstraNode(double d, uint64_t id, uint64_t p, NodeColor c);
        uint64_t NID; // maps this dijkstra node to a GraphNode.NID
        /// distance between this node and the target source node
        /// since our objective is to find the maximum likelihood path, we need to map probabilities onto a space that minimizes big probabilities and maximizes small ones
        /// -log(p) is how we do this
        double distance;
        /// minimum-distance predecessor of this node
        uint64_t predecessor;
        /// whether or not this node has been investigated
        NodeColor color;
    };
    /// Convenient for when Dijkstra's needs to figure out where to go next
    struct DijkstraCompare
    {
        using is_transparent = void;
        bool operator()(const DijkstraNode &lhs, const DijkstraNode &rhs) const
        {
            return lhs.distance < rhs.distance;
        }
    };
    /// @brief Implements Dijkstra's algorithm on a Cyclebite::Graph::Graph
    ///
    /// The input graph is transformed into a negative-log-space nodes in which maximum probabilities become minimum values
    /// @param graph    A set of nodes that should contain a cycle within them. 
    /// @param source   The place to start dijkstra's - if you are looking for a cycle, this should be equal to sink
    /// @param sink     The place to end dijkstra's - if you are looking for a cycle, this should be equal to source 
    /// @return         A set of Cyclebite::Graph::GraphNode::NID's that constitute the shortest path between source and sink
    std::set<uint64_t> Dijkstras(const Graph &graph, uint64_t source, uint64_t sink);
} // namespace Cyclebite::Graph