//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "CallGraphEdge.h"
#include "CallEdge.h"
#include "CallGraphNode.h"

using namespace Cyclebite::Graph;
using namespace std;

CallGraphEdge::CallGraphEdge() : UnconditionalEdge() {}

CallGraphEdge::CallGraphEdge(std::shared_ptr<CallGraphNode> sou, std::shared_ptr<CallGraphNode> sin, std::set<std::shared_ptr<CallEdge>, GECompare> calls) : UnconditionalEdge(0, sou, sin), calls(calls) {}

const std::set<std::shared_ptr<CallEdge>, GECompare> &CallGraphEdge::getCallEdges() const
{
    return calls;
}

const std::shared_ptr<CallGraphNode> CallGraphEdge::getChild() const
{
    return static_pointer_cast<CallGraphNode>(snk);
}

const std::shared_ptr<CallGraphNode> CallGraphEdge::getParent() const
{
    return static_pointer_cast<CallGraphNode>(src);
}