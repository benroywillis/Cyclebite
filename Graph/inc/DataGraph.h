//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include "Inst.h"
#include "Graph.h"

namespace Cyclebite::Graph
{
    class UnconditionalEdge;
    class DataGraph : public Graph
    {
    public:
        DataGraph();
        DataGraph(const std::set<std::shared_ptr<DataValue>, p_GNCompare> &nodeSet, const std::set<std::shared_ptr<UnconditionalEdge>, GECompare> &edgeSet);
        DataGraph(const std::set<std::shared_ptr<Inst>, p_GNCompare> &nodeSet, const std::set<std::shared_ptr<UnconditionalEdge>, GECompare> &edgeSet);
        const std::set<std::shared_ptr<DataValue>, p_GNCompare> getDataNodes() const;
    };
} // namespace Cyclebite::Graph