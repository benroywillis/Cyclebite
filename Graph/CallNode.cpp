//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "CallNode.h"
#include <llvm/IR/IntrinsicInst.h>

using namespace std;
using namespace Cyclebite::Graph;

CallNode::CallNode( const llvm::Instruction* inst, const set<shared_ptr<ControlBlock>, p_GNCompare>& dests ) : Inst(inst)
{
    for( const auto& d : dests )
    {
        destinations.insert(d);
    }
}

CallNode::CallNode(const Inst *upgrade, const set<shared_ptr<ControlBlock>, p_GNCompare>& dests ) : Inst(*upgrade)
{
    for( const auto& d : dests )
    {
        destinations.insert(d);
    }
}

const set<shared_ptr<ControlBlock>, p_GNCompare>& CallNode::getDestinations() const
{
    return destinations;
}

const set<shared_ptr<Inst>> CallNode::getDestinationFirstInsts() const
{
    set<shared_ptr<Inst>> insts;
    for( const auto& dest : destinations )
    {
        for( const auto& inst : dest->getInstructions() )
        {
            if( !llvm::isa<llvm::DbgInfoIntrinsic>(inst->getVal()) )
            {
                insts.insert(inst);
                break;
            }
        }
    }
    return insts;
}