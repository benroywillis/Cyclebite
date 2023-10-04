//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "ControlBlock.h"
#include "Inst.h"
#include "UnconditionalEdge.h"

using namespace Cyclebite::Graph;

ControlBlock::ControlBlock(std::shared_ptr<ControlNode> node, std::set<std::shared_ptr<Inst>, p_GNCompare> inst) : ControlNode(*node)
{
    instructions.insert(inst.begin(), inst.end());
}

uint64_t ControlBlock::getFrequency() const
{
    uint64_t total = 0;
    for( const auto& pred : predecessors )
    {
        total += std::static_pointer_cast<UnconditionalEdge>(pred)->getFreq();
    }
    return total;
}