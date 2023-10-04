// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
#include "ConditionalEdge.h"
#include "ControlNode.h"

using namespace Cyclebite::Graph;

ConditionalEdge::ConditionalEdge() : UnconditionalEdge() {}

ConditionalEdge::ConditionalEdge(const UnconditionalEdge &copy) : UnconditionalEdge(copy) {}

ConditionalEdge::ConditionalEdge(uint64_t count, std::shared_ptr<ControlNode> sou, std::shared_ptr<ControlNode> sin) : UnconditionalEdge(count, sou, sin) {}

void ConditionalEdge::setWeight(uint64_t sum)
{
    weight = (float)freq / (float)sum;
}

float ConditionalEdge::getWeight() const
{
    return weight;
}