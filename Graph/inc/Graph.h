#pragma once
#include "GraphNode.h"
#include <algorithm>

namespace Cyclebite::Graph
{
    class Graph
    {
    public:
        Graph();
        Graph(const std::set<std::shared_ptr<GraphNode>, p_GNCompare> &nodeSet, const std::set<std::shared_ptr<GraphEdge>, GECompare> &edgeSet);
        virtual ~Graph();
        const std::shared_ptr<GraphNode> &getOriginalNode(const std::shared_ptr<GraphNode> &s) const;
        const std::shared_ptr<GraphNode> &getOriginalNode(uint64_t ID) const;
        const std::set<std::shared_ptr<GraphNode>, p_GNCompare> &getNodes() const;
        const std::shared_ptr<GraphEdge> &getOriginalEdge(const std::shared_ptr<GraphEdge> &e) const;
        const std::set<std::shared_ptr<GraphEdge>, GECompare> &getEdges() const;
        const std::set<std::shared_ptr<GraphNode>, p_GNCompare> getFirstNodes() const;
        const std::set<std::shared_ptr<GraphNode>, p_GNCompare> getLastNodes() const;
        void addNode(const std::shared_ptr<GraphNode> &a);
        void addNodes(const std::set<std::shared_ptr<GraphNode>, p_GNCompare> &nodes);
        void removeNode(const std::shared_ptr<GraphNode> &r);
        void addEdge(const std::shared_ptr<GraphEdge> &a);
        void addEdges(const std::set<std::shared_ptr<GraphEdge>, GECompare> &a);
        void removeEdge(const std::shared_ptr<GraphEdge> &r);
        bool find(const std::shared_ptr<GraphNode> &s) const;
        bool find_node(uint64_t ID) const;
        bool find(const std::shared_ptr<GraphEdge> &s) const;
        bool empty() const;
        void clear();
        uint64_t node_count() const;
        uint64_t edge_count() const;
        uint64_t size() const;
        const std::shared_ptr<GraphNode> &operator[](const std::shared_ptr<GraphNode> &s) const;
        const std::shared_ptr<GraphEdge> &operator[](const std::shared_ptr<GraphEdge> &f) const;
        struct Node_Range
        {
            std::set<std::shared_ptr<GraphNode>, p_GNCompare>::iterator begin_;
            std::set<std::shared_ptr<GraphNode>, p_GNCompare>::iterator end_;
            std::set<std::shared_ptr<GraphNode>, p_GNCompare>::iterator begin() { return begin_; }
            std::set<std::shared_ptr<GraphNode>, p_GNCompare>::iterator end() { return end_; }
        };
        struct Edge_Range
        {
            std::set<std::shared_ptr<GraphEdge>, GECompare>::iterator begin_;
            std::set<std::shared_ptr<GraphEdge>, GECompare>::iterator end_;
            std::set<std::shared_ptr<GraphEdge>, GECompare>::iterator begin() { return begin_; }
            std::set<std::shared_ptr<GraphEdge>, GECompare>::iterator end() { return end_; }
        };
        Node_Range nodes();
        Edge_Range edges();
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

// John 3/11/22
// to fix the memory management problem, use references
// a reference contains a pointer to the real object, and the number of references made
// - this enables reference counting
// as things are being deleted
// - the children with 0 references can be safely deleted
// - the children with nonzero references cannot be safely deleted
// so add to the inheritence tree a parameter that counts references and implement this that way
// or build a reference object that is templated

// John 3/11/22 - On improving your speed of development
// 1. What does your pre-programming look like?
//   - generally drawing figures are the best thing
//   - atomize the work as much as possible
//     -> this makes tests small
//     -> example: a unit test that just runs the VirtualizeSubgraph() method
//   - What can I program and compile in 1-2h? And then write a unit test for it in 1-2h?
// 2. Longer-range plan
//   - What needs to work?
//   - In agile development, the goals are not concrete, but the goals are still known at all times
//   - What can be done in one week?
//   - What can be done in one month?
//   - What can be done in one quarter?
//   - What is aspirational but not deliverable?
//   - How well do we understand the problem?
