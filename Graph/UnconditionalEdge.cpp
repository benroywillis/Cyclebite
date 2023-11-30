//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "UnconditionalEdge.h"
#include "Util/Exceptions.h"
#include "ControlNode.h"
#include <memory>

using namespace std;
using namespace Cyclebite::Graph;

UnconditionalEdge::UnconditionalEdge() : GraphEdge(), freq(0) {}

UnconditionalEdge::UnconditionalEdge(std::shared_ptr<GraphNode> sou, std::shared_ptr<GraphNode> sin) : GraphEdge(sou, sin), freq(0)
{
    if (auto wsrc = dynamic_pointer_cast<ControlNode>(src))
    {
        weightedSrc = wsrc;
    }
    if (auto wsnk = dynamic_pointer_cast<ControlNode>(snk))
    {
        weightedSnk = wsnk;
    }
}

UnconditionalEdge::UnconditionalEdge(uint64_t count, std::shared_ptr<GraphNode> sou, std::shared_ptr<GraphNode> sin) : GraphEdge(sou, sin), freq(count)
{
    if (auto wsrc = dynamic_pointer_cast<ControlNode>(src))
    {
        weightedSrc = wsrc;
    }
    if (auto wsnk = dynamic_pointer_cast<ControlNode>(snk))
    {
        weightedSnk = wsnk;
    }
}

UnconditionalEdge::~UnconditionalEdge() = default;

const std::shared_ptr<ControlNode> &UnconditionalEdge::getWeightedSrc() const
{
    if (weightedSrc == nullptr)
    {
        throw CyclebiteException("Edge does not have a weighted source node!");
    }
    return weightedSrc;
}

const std::shared_ptr<ControlNode> &UnconditionalEdge::getWeightedSnk() const
{
    if (weightedSnk == nullptr)
    {
        throw CyclebiteException("Edge does not have a weighted sink node!");
    }
    return weightedSnk;
}

uint64_t UnconditionalEdge::getFreq() const
{
    return freq;
}

float UnconditionalEdge::getWeight() const
{
    return 1.0;
}