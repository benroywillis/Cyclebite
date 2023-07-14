#pragma once
#include "UnconditionalEdge.h"
#include <set>

namespace TraceAtlas::Graph
{
    class CallGraphNode;
    class CallEdge;
    class CallGraphEdge : public UnconditionalEdge
    {
    public:
        CallGraphEdge();
        CallGraphEdge(std::shared_ptr<CallGraphNode> sou, std::shared_ptr<CallGraphNode> sin, std::set<std::shared_ptr<CallEdge>, GECompare> calls);
        const std::set<std::shared_ptr<CallEdge>, GECompare> &getCallEdges() const;
        const std::shared_ptr<CallGraphNode> getChild() const;
        const std::shared_ptr<CallGraphNode> getParent() const;

    private:
        std::set<std::shared_ptr<CallEdge>, GECompare> calls;
    };
} // namespace TraceAtlas::Graph