#pragma once
#include "DataNode.h"
#include "Graph.h"

namespace TraceAtlas::Graph
{
    class UnconditionalEdge;
    class DataGraph : public Graph
    {
    public:
        DataGraph();
        DataGraph(const std::set<std::shared_ptr<DataNode>, p_GNCompare> &nodeSet, const std::set<std::shared_ptr<UnconditionalEdge>, GECompare> &edgeSet);
        const std::set<std::shared_ptr<DataNode>, p_GNCompare> getDataNodes() const;
    };
} // namespace TraceAtlas::Graph