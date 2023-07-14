#pragma once
#include "GraphEdge.h"
#include <cstdint>
#include <map>
#include <set>
#include <vector>

namespace Cyclebite::Graph
{
    class GraphNode
    {
    public:
        uint64_t NID;
        /// Meant to be constructed from a new block description in the input binary file
        virtual ~GraphNode();
        /// returns the predecessor edge pointer if this node is a predecessor of succ. Nullptr otherwise
        std::shared_ptr<GraphEdge> isPredecessor(std::shared_ptr<GraphNode> succ) const;
        /// returns the successor edge pointer if this node is a successor of pred. Nullptr otherwise
        std::shared_ptr<GraphEdge> isSuccessor(std::shared_ptr<GraphNode> pred) const;
        const std::set<std::shared_ptr<GraphEdge>, GECompare> &getPredecessors() const;
        const std::set<std::shared_ptr<GraphEdge>, GECompare> &getSuccessors() const;
        void addPredecessor(std::shared_ptr<GraphEdge> newEdge);
        void removePredecessor(std::shared_ptr<GraphEdge> oldEdge);
        void addSuccessor(std::shared_ptr<GraphEdge> newEdge);
        void removeSuccessor(std::shared_ptr<GraphEdge> oldEdge);

    protected:
        GraphNode();
        std::set<std::shared_ptr<GraphEdge>, GECompare> successors;
        std::set<std::shared_ptr<GraphEdge>, GECompare> predecessors;
        static uint64_t nextNID;
        static uint64_t getNextNID();
    };

    /// Allows for us to search a set of GraphNodes using an NID
    struct p_GNCompare
    {
        using is_transparent = void;
        bool operator()(const std::shared_ptr<GraphNode> &lhs, const std::shared_ptr<GraphNode> &rhs) const
        {
            return lhs->NID < rhs->NID;
        }
        bool operator()(const std::shared_ptr<GraphNode> &lhs, uint64_t rhs) const
        {
            return lhs->NID < rhs;
        }
        bool operator()(uint64_t lhs, const std::shared_ptr<GraphNode> &rhs) const
        {
            return lhs < rhs->NID;
        }
    };

    struct GNCompare
    {
        using is_transparent = void;
        bool operator()(const GraphNode &lhs, const GraphNode &rhs) const
        {
            return lhs.NID < rhs.NID;
        }
        bool operator()(const GraphNode &lhs, uint64_t rhs) const
        {
            return lhs.NID < rhs;
        }
        bool operator()(uint64_t lhs, const GraphNode &rhs) const
        {
            return lhs < rhs.NID;
        }
    };
} // namespace Cyclebite::Graph

// John 3/2/22
// The abstraction is now hurting
//  - the difference between predecessors, successors in GraphNode, ControlNode is taking up more 2x memory