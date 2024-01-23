//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include <cstdint>
#include <memory>

namespace Cyclebite::Graph
{
    class GraphNode;
    class GraphEdge
    {
    public:
        GraphEdge();
        GraphEdge(std::shared_ptr<GraphNode> sou, std::shared_ptr<GraphNode> sin);
        GraphEdge(uint64_t ID);
        GraphEdge(uint64_t ID, std::shared_ptr<GraphNode> sou, std::shared_ptr<GraphNode> sin);
        virtual ~GraphEdge();
        uint64_t ID() const;
        bool hasWeightedSrc() const;
        bool hasWeightedSnk() const;
        const std::shared_ptr<GraphNode> &getSrc() const;
        const std::shared_ptr<GraphNode> &getSnk() const;
        virtual float getWeight() const;
    protected:
        uint64_t EID;
        float weight;
        std::shared_ptr<GraphNode> src;
        std::shared_ptr<GraphNode> snk;
        static uint64_t nextEID;
        static uint64_t getNextEID();
    };
    /// Allows for us to search a set of GraphEdges using an EID
    struct GECompare
    {
        bool operator()(const std::shared_ptr<GraphEdge> &lhs, const std::shared_ptr<GraphEdge> &rhs) const
        {
            if (lhs->getSrc() == rhs->getSrc())
            {
                return lhs->getSnk() < rhs->getSnk();
            }
            return lhs->getSrc() < rhs->getSrc();
        }
    };
} // namespace Cyclebite::Graph