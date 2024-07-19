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
    /// Cyclebite::Graph::Datagraph holds Cyclebite::Graph::DataValues, which sit on top of llvm::Value's from the static program
    /// Each node is a DataValue (or perhaps a Cyclebite::Graph::Inst, an upgraded DataValue)
    /// Each edge is an UnconditionalEdge (because LLVM-IR is SSA)
    class DataGraph : public Graph
    {
    public:
        DataGraph();
        DataGraph(const std::set<std::shared_ptr<DataValue>, p_GNCompare> &nodeSet, const std::set<std::shared_ptr<UnconditionalEdge>, GECompare> &edgeSet);
        DataGraph(const std::set<std::shared_ptr<Inst>, p_GNCompare> &nodeSet, const std::set<std::shared_ptr<UnconditionalEdge>, GECompare> &edgeSet);
        const std::set<std::shared_ptr<DataValue>, p_GNCompare> getDataNodes() const;
    };
} // namespace Cyclebite::Graph