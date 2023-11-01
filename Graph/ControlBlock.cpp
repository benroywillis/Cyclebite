//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "ControlBlock.h"
#include "Inst.h"
#include "DataValue.h"
#include "UnconditionalEdge.h"

using namespace std;
using namespace Cyclebite::Graph;

ControlBlock::ControlBlock(std::shared_ptr<ControlNode> node, std::set<std::shared_ptr<Inst>, p_GNCompare> inst) : ControlNode(*node)
{
    instructions.insert(inst.begin(), inst.end());
}

const set<shared_ptr<Inst>, p_GNCompare>& ControlBlock::getInstructions() const
{
    return instructions;
}

bool ControlBlock::find( const shared_ptr<DataValue>& f ) const
{
    return instructions.contains(f);
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

void ControlBlock::addInstruction( const shared_ptr<Inst>& newInst )
{
    instructions.insert(newInst);
}