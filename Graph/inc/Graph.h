//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include "GraphNode.h"
#include <algorithm>

namespace Cyclebite::Graph
{
    /// A general class for a graph of GraphNodes and GraphEdges
    class Graph
    {
    public:
        Graph();
        Graph(const std::set<std::shared_ptr<GraphNode>, p_GNCompare> &nodeSet, const std::set<std::shared_ptr<GraphEdge>, GECompare> &edgeSet);
        virtual ~Graph();
        /// @brief Returns the node in the graph whose Cyclebite::Graph::GraphNode::NID matches the NID of s
        const std::shared_ptr<GraphNode> &getOriginalNode(const std::shared_ptr<GraphNode> &s) const;
        /// @brief Returns the Cyclebite::Graph::GraphNode whose NID matches ID
        const std::shared_ptr<GraphNode> &getOriginalNode(uint64_t ID) const;
        /// @brief Returns all nodes in the graph in their parent-most class
        const std::set<std::shared_ptr<GraphNode>, p_GNCompare> &getNodes() const;
        /// @brief Returns the Cyclebite::Graph::GraphEdge whose EID matches e->getEID()
        const std::shared_ptr<GraphEdge> &getOriginalEdge(const std::shared_ptr<GraphEdge> &e) const;
        /// @brief Returns all GraphEdge's in the graph
        /// @return 
        const std::set<std::shared_ptr<GraphEdge>, GECompare> &getEdges() const;
        /// @brief Returns the nodes in the graph that have no predecessors
        const std::set<std::shared_ptr<GraphNode>, p_GNCompare> getFirstNodes() const;
        /// @brief Returns the nodes in the graph that have no successors
        const std::set<std::shared_ptr<GraphNode>, p_GNCompare> getLastNodes() const;
        void addNode(const std::shared_ptr<GraphNode> &a);
        void addNodes(const std::set<std::shared_ptr<GraphNode>, p_GNCompare> &nodes);
        /// Removes the node(s) in the graph whose Cyclebite::Graph::GraphNode::NID matches that of r
        void removeNode(const std::shared_ptr<GraphNode> &r);
        void addEdge(const std::shared_ptr<GraphEdge> &a);
        void addEdges(const std::set<std::shared_ptr<GraphEdge>, GECompare> &a);
        /// Removes the Cyclebite::Graph::GraphEdge whose EID matches that of r
        void removeEdge(const std::shared_ptr<GraphEdge> &r);
        /// Returns true if there is a node whose NID matches that of s, false otherwise
        bool find(const std::shared_ptr<GraphNode> &s) const;
        /// Returns true if there is a node whose NID matches ID, false otherwise
        bool find_node(uint64_t ID) const;
        /// Returns true if there is an edge whose EID matches that of s, false otherwise
        bool find(const std::shared_ptr<GraphEdge> &s) const;
        /// Returns true if there are there are both no nodes and no edges in the graph, false otherwise 
        bool empty() const;
        /// Removes all nodes and edges from this graph
        void clear();
        uint64_t node_count() const;
        uint64_t edge_count() const;
        /// Returns the sum of all member nodes and all member edges in the graph
        uint64_t size() const;
        /// Returns the node whose NID matches that of s
        /// Throws a CyclebiteException if no node matches the NID in s
        /// If there is more than one node with the NID of s, a random node with the same NID as s is returned
        const std::shared_ptr<GraphNode> &operator[](const std::shared_ptr<GraphNode> &s) const;
        /// Returns the edge whose EID matches that of f
        /// Throws a CyclebiteException if no edges in the graph match the EID of f
        /// If there is more than one edge whose EID is that of f, a random edge with the same EID as f is returned
        const std::shared_ptr<GraphEdge> &operator[](const std::shared_ptr<GraphEdge> &f) const;
        /// Useful for defining iterator ranges over the nodes of the graph
        struct Node_Range
        {
            std::set<std::shared_ptr<GraphNode>, p_GNCompare>::iterator begin_;
            std::set<std::shared_ptr<GraphNode>, p_GNCompare>::iterator end_;
            std::set<std::shared_ptr<GraphNode>, p_GNCompare>::iterator begin() { return begin_; }
            std::set<std::shared_ptr<GraphNode>, p_GNCompare>::iterator end() { return end_; }
        };
        /// Useful for defining iterator ranges over the edges of the graph
        struct Edge_Range
        {
            std::set<std::shared_ptr<GraphEdge>, GECompare>::iterator begin_;
            std::set<std::shared_ptr<GraphEdge>, GECompare>::iterator end_;
            std::set<std::shared_ptr<GraphEdge>, GECompare>::iterator begin() { return begin_; }
            std::set<std::shared_ptr<GraphEdge>, GECompare>::iterator end() { return end_; }
        };
        Node_Range nodes();
        Edge_Range edges();
        /// Const version of the above
        struct Const_Node_Range
        {
            const std::set<std::shared_ptr<GraphNode>, p_GNCompare>::iterator begin_;
            const std::set<std::shared_ptr<GraphNode>, p_GNCompare>::iterator end_;
            const std::set<std::shared_ptr<GraphNode>, p_GNCompare>::iterator begin() const { return begin_; }
            const std::set<std::shared_ptr<GraphNode>, p_GNCompare>::iterator end() const { return end_; }
        };
        struct Const_Edge_Range
        {
            const std::set<std::shared_ptr<GraphEdge>, GECompare>::iterator begin_;
            const std::set<std::shared_ptr<GraphEdge>, GECompare>::iterator end_;
            const std::set<std::shared_ptr<GraphEdge>, GECompare>::iterator begin() const { return begin_; }
            const std::set<std::shared_ptr<GraphEdge>, GECompare>::iterator end() const { return end_; }
        };
        const Const_Node_Range nodes() const;
        const Const_Edge_Range edges() const;

    protected:
        std::set<std::shared_ptr<GraphNode>, p_GNCompare> nodeSet;
        std::set<std::shared_ptr<GraphEdge>, GECompare> edgeSet;
    };
    /// Useful for downcasting a structure of derived types to the base type
    template <typename N, typename C>
    inline std::set<std::shared_ptr<GraphNode>, p_GNCompare> NodeConvert(const std::set<std::shared_ptr<N>, C> &derived)
    {
        std::set<std::shared_ptr<GraphNode>, p_GNCompare> converted;
        std::transform(derived.begin(), derived.end(), std::inserter(converted, converted.begin()), [](const std::shared_ptr<N> &down) { return std::static_pointer_cast<GraphNode>(down); });
        return converted;
    }
    template <typename E, typename C>
    inline std::set<std::shared_ptr<GraphEdge>, GECompare> EdgeConvert(const std::set<std::shared_ptr<E>, C> &derived)
    {
        std::set<std::shared_ptr<GraphEdge>, GECompare> converted;
        std::transform(derived.begin(), derived.end(), std::inserter(converted, converted.begin()), [](const std::shared_ptr<E> &down) { return std::static_pointer_cast<GraphEdge>(down); });
        return converted;
    }
    template <typename N>
    inline std::set<std::shared_ptr<N>, p_GNCompare> Upcast(const std::set<std::shared_ptr<GraphNode>, p_GNCompare> &derived)
    {
        std::set<std::shared_ptr<N>, p_GNCompare> converted;
        std::transform(derived.begin(), derived.end(), std::inserter(converted, converted.begin()), [](const std::shared_ptr<GraphNode> &down) { return std::static_pointer_cast<N>(down); });
        return converted;
    }
} // namespace Cyclebite::Graph