//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include "UnconditionalEdge.h"
#include <set>

namespace Cyclebite::Graph
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
} // namespace Cyclebite::Graph