//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "CallGraphNode.h"
#include "CallGraphEdge.h"

using namespace Cyclebite::Graph;
using namespace std;

CallGraphNode::CallGraphNode(const llvm::Function *F) : GraphNode(), f(F) {}

const llvm::Function *CallGraphNode::getFunction() const
{
    return f;
}

const std::set<std::shared_ptr<CallGraphEdge>, GECompare> CallGraphNode::getChildren() const
{
    std::set<std::shared_ptr<CallGraphEdge>, GECompare> edges;
    for (const auto &e : getSuccessors())
    {
        edges.insert(static_pointer_cast<CallGraphEdge>(e));
    }
    return edges;
}

const std::set<std::shared_ptr<CallGraphEdge>, GECompare> CallGraphNode::getParents() const
{
    std::set<std::shared_ptr<CallGraphEdge>, GECompare> edges;
    for (const auto &e : getPredecessors())
    {
        edges.insert(static_pointer_cast<CallGraphEdge>(e));
    }
    return edges;
}