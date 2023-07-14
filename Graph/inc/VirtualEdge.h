#pragma once
#include "ConditionalEdge.h"
#include <set>

namespace TraceAtlas::Graph
{
    class VirtualEdge : public ConditionalEdge
    {
    public:
        VirtualEdge();
        VirtualEdge(uint64_t frequency, std::shared_ptr<ControlNode> sou, std::shared_ptr<ControlNode> sin, std::set<std::shared_ptr<UnconditionalEdge>, GECompare> &newEdges);
        bool addEdge(const std::shared_ptr<UnconditionalEdge> newEdge);
        void addEdges(const std::set<std::shared_ptr<UnconditionalEdge>, GECompare> newEdges);
        const std::set<std::shared_ptr<UnconditionalEdge>, GECompare> &getEdges() const;
        bool isCallEdge() const;

    private:
        std::set<std::shared_ptr<UnconditionalEdge>, GECompare> edges;
    };
} // namespace TraceAtlas::Graph