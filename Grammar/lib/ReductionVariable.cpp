//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "ReductionVariable.h"
#include "Graph/inc/IO.h"
#include "Task.h"
#include "Util/Print.h"
#include "Util/Exceptions.h"
#include <llvm/IR/IntrinsicInst.h>
#include <deque>

using namespace std;
using namespace Cyclebite::Grammar;

ReductionVariable::ReductionVariable( const shared_ptr<InductionVariable>& iv, const shared_ptr<Cyclebite::Graph::DataValue>& n, const shared_ptr<Cyclebite::Graph::DataValue>& addr ) : Symbol("rv"), iv(iv), node(n), address(addr)
{
    // incoming datanode must map to a binary operation
    if( const auto& op = llvm::dyn_cast<llvm::BinaryOperator>(n->getVal()) )
    {
        bin = Cyclebite::Graph::GetOp(op->getOpcode());
    }
    else if( const auto& intrin = llvm::dyn_cast<llvm::IntrinsicInst>(node->getVal()) )
    {
        if( llvm::Intrinsic::getBaseName( intrin->getIntrinsicID() ) != "llvm.fmuladd" )
        {
            PrintVal(node->getVal());
            throw CyclebiteException("Cannot yet handle this intrinsic as a reduction variable!");
        }
        bin = Cyclebite::Graph::Operation::fadd;
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

const shared_ptr<Cyclebite::Graph::DataValue>& ReductionVariable::getAddress() const
{
    return address;
}

/*const llvm::PHINode* ReductionVariable::getPhi() const
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
}*/

set<shared_ptr<ReductionVariable>> Cyclebite::Grammar::getReductionVariables(const shared_ptr<Task>& t, const set<shared_ptr<InductionVariable>>& vars)
{
    set<shared_ptr<ReductionVariable>> rvs;
    // these stores have a value operand that comes from the functional group
    set<const llvm::StoreInst*> sts;
    for( const auto& c : t->getCycles() )
    {
        for( const auto& b : c->getBody() )
        {
            for( const auto& i : b->getInstructions() )
            {
                if( const auto st = llvm::dyn_cast<llvm::StoreInst>(i->getVal()) )
                {
                    if( const auto& v = llvm::dyn_cast<llvm::Instruction>(st->getValueOperand()) )
                    {
                        if( static_pointer_cast<Cyclebite::Graph::Inst>(Cyclebite::Graph::DNIDMap.at(v))->isFunction() )
                        {
                            sts.insert(st);
                        }
                    }
                }
            }
        }
    }
    for( const auto& s : sts )
    {
        // reduction variables search
        // walk the value operands of those instructions to start the hunt for reduction variables
        // reduction variables (rv) look very much like induction variables (iv) in that they commonly come in two flavors
        // 1. they are loaded from and stored to through indirect pointers (like directly storing to the array index instead of a local value, common in unoptimized cases)
        // 2. they loop with a phi (common to optimized cases)
        // the difference between rv and iv is that rv lies entirely within a functional group
        // thus we crawl the functional group and put each value through checks similar to the getInductionVariables() method
        // we have a multi-destination control instruction, walk its predecessors to find a memory or binary operation that indicates an induction variable
        set<shared_ptr<Cyclebite::Graph::DataValue>> reductionCandidates;
        deque<const llvm::Instruction*> Q;
        set<const llvm::Instruction*> seen;
        shared_ptr<Cyclebite::Graph::DataValue> reductionOp = nullptr;
        Q.push_front(llvm::cast<llvm::Instruction>(s));
        seen.insert(llvm::cast<llvm::Instruction>(s));
        while( !Q.empty() )
        {
            for( auto& use : Q.front()->operands() )
            {
                // binary instructions should be the only type of instruction to lead us to a phi
                if( auto bin = llvm::dyn_cast<llvm::BinaryOperator>(use.get()) )
                {
                    if( seen.find(bin) == seen.end() )
                    {
                        // we only mark the binary op we see that is closest to the store
                        if( !reductionOp )
                        {
                            reductionOp = Cyclebite::Graph::DNIDMap.at(bin);
                        }
                        Q.push_back(bin);
                        seen.insert(bin);
                    }
                }
                else if( auto phi = llvm::dyn_cast<llvm::PHINode>(use.get()) )
                {
                    // case found in optimized programs when an induction variable lives in a value (not the heap) and has a DFG cycle between an add and a phi node
                    // in order for this phi to be a reduction variable candidate, it must form a cycle with a binary operator
                    // we only expect this cycle to involve two nodes, thus a check of the phis users suffices to find the complete cycle
                    if( auto bin = llvm::dyn_cast<llvm::BinaryOperator>(Q.front()) )
                    {
                        if( Cyclebite::Graph::DNIDMap.at(bin)->isPredecessor(Cyclebite::Graph::DNIDMap.at(phi)) )
                        {
                            // we have found a cycle between a binary op and a phi, likely indicating an induction variable, thus add it to the set of dimensions
                            seen.insert(bin);
                            seen.insert(phi);
                            reductionOp = Cyclebite::Graph::DNIDMap.at(bin);
                            reductionCandidates.insert(Cyclebite::Graph::DNIDMap.at(phi));
                        }
                    }
                    // sometimes llvm will insert intrinsics into the code that hide reductions
                    // llvm.fmuladd is an example
                    // so look for an llvm intrinsic that can create a reduction
                    else if( const auto& intrin = llvm::dyn_cast<llvm::IntrinsicInst>(Q.front()) )
                    {
                        auto intrinName = string(llvm::Intrinsic::getBaseName(intrin->getIntrinsicID()));
                        if( intrinName == "llvm.fmuladd" )
                        {
                            // confirm the phi is the third argument in the function call - this is a 
                            if( intrin->getNumOperands() == 4 ) // three arg operands + the function
                            {
                                if( phi == intrin->getOperand(2) )
                                {
                                    // confirmed, this phi is a reduction candidate
                                    seen.insert(intrin);
                                    seen.insert(phi);
                                    reductionOp = Cyclebite::Graph::DNIDMap.at(intrin);
                                    reductionCandidates.insert(Cyclebite::Graph::DNIDMap.at(phi));
                                }
                            }
                        }
                        else
                        {
                            spdlog::warn("Cannot yet handle this intrinsic when evaluating reduction variables:");
                            PrintVal(intrin);
                        }
                    }
                }
                else if( auto ld = llvm::dyn_cast<llvm::LoadInst>(use.get()) )
                {
                    // a load's pointer may point us back to a store we have seen
                    // this will lead us back to a reduction variable pointer (in the case of unoptimized code)
                    if( reductionOp )
                    {
                        // first piece of criteria: we have a ld/st pair that uses the same pointer and saves the reductionOp
                        if( (s->getPointerOperand() == ld->getPointerOperand()) && (s->getValueOperand() == reductionOp->getVal()) )
                        {
                            bool candidate = true;
                            // second piece: the pointer must be constant throughout the local-most cycle
                            // (see PERFECT/2DConv BB 5, which is basically a zip with a coefficient on each element)
                            // the cycle has an iterator, and if that iterator is used to offset the pointer on each cycle iteration, this is not a reduction
                            shared_ptr<Cycle> c = nullptr;
                            for( const auto& cy : t->getCycles() )
                            {
                                if( cy->find(reductionOp) )
                                {
                                    c = cy;
                                    break;
                                }
                            }
                            if( !c )
                            {
                                throw CyclebiteException("Could not find the cycle of the reductionOp when finding reduction variable candidates!");
                            }
                            const shared_ptr<Cyclebite::Graph::DataValue> ptr = Cyclebite::Graph::DNIDMap.at(ld->getPointerOperand());
                            for( const auto& iv : vars )
                            {
                                if( c->find(iv->getNode()) )
                                {
                                    if( iv->isOffset(ld->getPointerOperand()) )
                                    {
                                        // the load pointer is an offset of the loop iterator, thus we are not reducing a value into it
                                        candidate = false;
                                        break;
                                    }
                                }
                            }
                            if( candidate )
                            {
                                reductionCandidates.insert(Cyclebite::Graph::DNIDMap.at(s->getPointerOperand()));
                            }
                        }
                    }
                    seen.insert(ld);
                }
                // sometimes llvm will insert intrinsics into the code that hide reductions
                // llvm.fmuladd is an example
                // so look for an llvm intrinsic that can create a reduction
                else if( const auto& intrin = llvm::dyn_cast<llvm::IntrinsicInst>(use.get()) )
                {
                    auto intrinName = string(llvm::Intrinsic::getBaseName(intrin->getIntrinsicID()));
                    if( intrinName == "llvm.fmuladd" )
                    {
                        // this is a reductionOp candidate
                        reductionOp = Cyclebite::Graph::DNIDMap.at(intrin);
                    }
                    else
                    {
                        spdlog::warn("Cannot yet handle this intrinsic when evaluating reduction variables:");
                        PrintVal(intrin);
                    }
                    Q.push_back(intrin);
                    seen.insert(intrin);
                }
                else if( auto st = llvm::dyn_cast<llvm::StoreInst>(use.get()) )
                {
                    // shouldn't encounter this case, we started from the store and walked backwards
                    seen.insert(st);
                }
            }
            Q.pop_front();
        }
        // now find the induction variable associated with each reduction variable candidate
        for( const auto& can : reductionCandidates )
        {
            // it is possible to find the same reduction candidate twice when operations are partially unrolled
            // to handle this case we do a check on the rvs that already exist and skip the ones that may be a repeat
            bool skip = false;
            for( const auto& rv : rvs )
            {
                if( reductionOp == rv->getNode() )
                {
                    skip = true;
                    break;
                }
            }
            if( skip ) { continue; }
            shared_ptr<InductionVariable> iv = nullptr;
            // the store value operand is within the cycle that contains the RV
            if( const auto& v = llvm::dyn_cast<llvm::Instruction>(s->getValueOperand()) )
            {
                auto b = static_pointer_cast<Cyclebite::Graph::Inst>(Cyclebite::Graph::DNIDMap.at(v));
                for( const auto& var : vars )
                {
                    if( var->getCycle()->find(b) )
                    {
                        iv = var;
                    }
                }
            }
            else
            {
                PrintVal(s);
                throw CyclebiteException("Store instruction of an induction variable is not an instruction!");
            }
            if( iv == nullptr )
            {
                PrintVal(can->getVal());
                throw CyclebiteException("Cannot map this reduction variable to an induction variable!");
            }
            rvs.insert( make_shared<ReductionVariable>(iv, reductionOp, can) );
        }
    }
    return rvs;
}
