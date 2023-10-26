//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "Reduction.h"
#include "Task.h"
#include "Util/Exceptions.h"
#include "Util/Print.h"
#include <llvm/IR/IntrinsicInst.h>

using namespace std;
using namespace Cyclebite::Grammar;

Reduction::Reduction(const shared_ptr<Task>& ta, 
                     const shared_ptr<ReductionVariable>& var, 
                     const vector<shared_ptr<Symbol>>& in, 
                     const vector<Cyclebite::Graph::Operation>& o, 
                     const shared_ptr<Symbol>& out ) : Expression(ta, in, o, out, "reduction"), rv(var) {}

const shared_ptr<ReductionVariable>& Reduction::getRV() const
{
    return rv;
}

const shared_ptr<Cycle>& Reduction::getReductionCycle() const
{
    set<shared_ptr<Cycle>> reduxCycles;
    for( const auto& c : t->getCycles() )
    {
        if( c->find(rv->getAddress()) )
        {
            return c;
        }
    }
    throw CyclebiteException("Could not find reduction variable cycle!");
}

bool Reduction::isParallelReduction() const
{
    // cases
    // 1. I use regular binary opts to do the reduction
    //    - then its just a trivial check on whether or not the reduction variable is in the reduction expression
    // 2. I use an intrinsic (like llvm::fmuladd(add0, add1, RV))
    //    - then I only check the first two operands
    if( const auto& intrin = llvm::dyn_cast<llvm::IntrinsicInst>(rv->getNode()->getVal()) )
    {
        if( llvm::Intrinsic::getBaseName( intrin->getIntrinsicID() ) == "llvm.fmuladd" )
        {
            // the first two ops are the only ones we have to check
            for( unsigned i = 0; i < 2; i++ )
            {
                if( intrin->getOperand(i) == rv->getNode()->getVal() )
                {
                    return false;
                }
            }
            return true;
        }
        else
        {
            PrintVal(intrin);
            throw CyclebiteException("Cannot yet handle this intrinsic when determining reduction parallelism!");
        }
    }
    else
    {
        // this set contains the predecessors to the reduction variable... these are the instructions that may contain the reduction variable and imply a loop-loop dependence
        set<const llvm::Value*> preds;
        if( const auto& inst = llvm::dyn_cast<llvm::Instruction>(rv->getNode()->getVal()) )
        {
            for( const auto& op : inst->operands() )
            {
                preds.insert(op.get());
            }
            for( const auto& pred : preds )
            {
                if( pred == rv->getNode()->getVal() )
                {
                    return false;
                }
            }
        }
        return true;
    }
}

string Reduction::dump() const
{
    string expr = "";
    bool flip = false;
    if( !printedName )
    {
        flip = true;
        if( output )
        {
            expr += output->dump() + " <- ";
        }
        expr += name + " " + Cyclebite::Graph::OperationToString.at(rv->getOp()) + "= ";
    }
    printedName = true;
    if( !symbols.empty() )
    {
        auto b = symbols.begin();
        auto o = ops.begin();
        expr += " "+(*b)->dump();
        b = next(b);
        while( b != symbols.end() )
        {
            expr += " "+string(Cyclebite::Graph::OperationToString.at(*o))+" "+(*b)->dump();
            b = next(b);
            o = next(o);
        }
    }
    printedName = flip ? !printedName : printedName;
    return expr;
}