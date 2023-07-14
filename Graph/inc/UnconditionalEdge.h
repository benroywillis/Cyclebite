#pragma once
#include <memory>
#include "GraphEdge.h"

namespace Cyclebite::Graph
{
    class GraphNode;
    class ControlNode;
    class UnconditionalEdge : public GraphEdge
    {
    public:
        UnconditionalEdge();
        UnconditionalEdge(std::shared_ptr<GraphNode> sou, std::shared_ptr<GraphNode> sin);
        UnconditionalEdge(uint64_t freq, std::shared_ptr<GraphNode> sou, std::shared_ptr<GraphNode> sin);
        ~UnconditionalEdge();
        const std::shared_ptr<ControlNode> &getWeightedSrc() const;
        const std::shared_ptr<ControlNode> &getWeightedSnk() const;
        uint64_t getFreq() const;
        virtual float getWeight() const;

    protected:
        std::shared_ptr<ControlNode> weightedSrc;
        std::shared_ptr<ControlNode> weightedSnk;
        uint64_t freq;
    };
} // namespace Cyclebite::Graph