//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "GraphEdge.h"
#include "ControlNode.h"

using namespace Cyclebite::Graph;

uint64_t GraphEdge::nextEID = 0;

GraphEdge::GraphEdge() : EID(getNextEID()), weight(0.0f) {}

GraphEdge::GraphEdge(std::shared_ptr<GraphNode> sou, std::shared_ptr<GraphNode> sin) : EID(getNextEID()), weight(0.0f), src(sou), snk(sin) {}

GraphEdge::GraphEdge(uint64_t ID)
{
    EID = ID;
    if (nextEID < EID)
    {
        nextEID = EID + 1;
    }
}

GraphEdge::GraphEdge(uint64_t ID, std::shared_ptr<GraphNode> sou, std::shared_ptr<GraphNode> sin)
{
    EID = ID;
    if (nextEID < EID)
    {
        nextEID = EID + 1;
    }
    src = sou;
    snk = sin;
}

GraphEdge::~GraphEdge() = default;

uint64_t GraphEdge::ID() const
{
    return EID;
}

uint64_t GraphEdge::getNextEID()
{
    return nextEID++;
}

bool GraphEdge::hasWeightedSrc() const
{
    if (auto wsrc = std::dynamic_pointer_cast<ControlNode>(src))
    {
        return true;
    }
    return false;
}

bool GraphEdge::hasWeightedSnk() const
{
    if (auto wsnk = std::dynamic_pointer_cast<ControlNode>(snk))
    {
        return true;
    }
    return false;
}

const std::shared_ptr<GraphNode> &GraphEdge::getSrc() const
{
    return src;
}

const std::shared_ptr<GraphNode> &GraphEdge::getSnk() const
{
    return snk;
}

float GraphEdge::getWeight() const
{
    return weight;
}