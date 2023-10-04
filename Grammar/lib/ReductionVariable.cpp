// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
#include "ReductionVariable.h"
#include "Util/Print.h"
#include "Util/Exceptions.h"
#include <llvm/IR/IntrinsicInst.h>
#include <deque>

using namespace std;
using namespace Cyclebite::Grammar;

ReductionVariable::ReductionVariable( const shared_ptr<InductionVariable>& iv, const shared_ptr<Cyclebite::Graph::DataValue>& n ) : Symbol("rv"), iv(iv), node(n)
{
    if( const auto& intrin = llvm::dyn_cast<llvm::IntrinsicInst>(node->getVal()) )
    {
        if( llvm::Intrinsic::getBaseName( intrin->getIntrinsicID() ) != "llvm.fmuladd" )
        {
            PrintVal(node->getVal());
            throw CyclebiteException("Cannot yet handle this intrinsic as a reduction variable!");
        }
    }
    // incoming datanode must map to a binary operation
    if( const auto& op = llvm::dyn_cast<llvm::BinaryOperator>(n->getVal()) )
    {
        bin = Cyclebite::Graph::GetOp(op->getOpcode());
    }
}

string ReductionVariable::dump() const 
{
    return name;
}

Cyclebite::Graph::Operation ReductionVariable::getOp() const
{
    return bin;
}

const shared_ptr<Cyclebite::Graph::DataValue>& ReductionVariable::getNode() const
{
    return node;
}

const llvm::PHINode* ReductionVariable::getPhi() const
{
    set<const llvm::PHINode*> phis;
    deque<const llvm::Value*> Q;
    set<const llvm::Value*> covered;
    Q.push_front(node->getVal());
    while( !Q.empty() )
    {
        if( const auto& phi = llvm::dyn_cast<llvm::PHINode>(Q.front()) )
        {
            phis.insert(phi);
            covered.insert(phi);
        }
        else if( const auto& inst = llvm::dyn_cast<llvm::Instruction>(Q.front()) )
        {
            for( const auto& user : inst->users() )
            {
                if( covered.find(user) == covered.end() )
                {
                    Q.push_back(user);
                    covered.insert(user);
                }
            }
        }
        Q.pop_front();
    }
    for( const auto& phi : phis )
    {
        // if this phi loops with our value, its our phi
        // we already know from our forward walk that this phi uses our node
        // so if our node uses this phi, we have a loop
        Q.push_front(phi);
        covered.clear();
        covered.insert(phi);
        while( !Q.empty() )
        {
            if( Q.front() == node->getVal() )
            {
                // there's a special case when the RV node is an llvm intrinsic
                // the arguments to the intrinsic may be from an operation that does not involve a reduction (e.g., the first two args in llvm.fmuladd are for the mul, the rv is the third)
                // thus we must make sure this phi is transacting with the RV, not the other operands
                if( const auto& intrin = llvm::dyn_cast<llvm::IntrinsicInst>(node->getVal()) )
                {
                    if( string(llvm::Intrinsic::getBaseName( intrin->getIntrinsicID() )) == "llvm.fmuladd" )
                    {
                        if( intrin->getOperand(2) == phi )
                        {
                            return phi;
                        }
                        // else we don't care about this case
                    }
                }
                else
                {
                    // the loop has been completed, return this phi
                    return phi;
                }
            }
            else
            {
                for( const auto& user : Q.front()->users() )
                {
                    if( covered.find(user) == covered.end() )
                    {
                        Q.push_back(user);
                        covered.insert(user);
                    }
                }
            }
            Q.pop_front();
        }
    }
    return nullptr;
}