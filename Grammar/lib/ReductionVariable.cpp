//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "ReductionVariable.h"
#include "Graph/inc/IO.h"
#include "Graph/inc/Dijkstra.h"
#include "Task.h"
#include "Util/Print.h"
#include "Util/Exceptions.h"
#include <llvm/IR/IntrinsicInst.h>
#include <deque>

using namespace std;
using namespace Cyclebite::Grammar;

ReductionVariable::ReductionVariable( const set<shared_ptr<Dimension>, DimensionSort>& dims, 
                                      const vector<shared_ptr<Graph::DataValue>>& addrs, 
                                      const shared_ptr<Cyclebite::Graph::DataValue>& n ) : Symbol("rv"), dimensions(dims), addresses(addrs), node(n)
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

const set<shared_ptr<Dimension>, DimensionSort>& ReductionVariable::getDimensions() const
{
    return dimensions;
}

const vector<shared_ptr<Cyclebite::Graph::DataValue>>& ReductionVariable::getAddresses() const
{
    return addresses;
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
                if( const auto& cast = llvm::dyn_cast<llvm::CastInst>(use.get()) )
                {
                    if( !seen.contains(cast) )
                    {
                        Q.push_back(cast);
                        seen.insert(cast);
                    }
                }
                // binary instructions should be the only type of instruction to lead us to a phi
                else if( auto bin = llvm::dyn_cast<llvm::BinaryOperator>(use.get()) )
                {
                    if( !seen.contains(bin) )
                    {
                        // we only mark the binary op we see that is closest to the store
                        if( !reductionOp )
                        {
                            reductionOp = Cyclebite::Graph::DNIDMap.at(bin);
                        }
                        Q.push_back(bin);
                        seen.insert(bin);
                    }
                    // sometimes we have to evaluate the case where the current instruction under investigation is a phi and its operand is a binary
                    // (this is just the reverse case of phi operand to a binary op - they both cycle with each other so both cases are equivalent)
                    for( const auto& user : bin->users() )
                    {
                        if( const auto& phi = llvm::dyn_cast<llvm::PHINode>(user) )
                        {
                            // make sure it cycles with this bin
                            if( Cyclebite::Graph::DNIDMap.at(phi)->isPredecessor(Cyclebite::Graph::DNIDMap.at(bin)) )
                            {
                                // we have found a cycle between a phi and a binary op, likely indicating a reduction variable
                                seen.insert(bin);
                                seen.insert(phi);
                                reductionOp = Cyclebite::Graph::DNIDMap.at(bin);
                                reductionCandidates.insert(Cyclebite::Graph::DNIDMap.at(phi));
                            }
                        }
                    }
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
                    if( !seen.contains(intrin) )
                    {
                        Q.push_back(intrin);
                        seen.insert(intrin);
                    }
                    // sometimes we have to evaluate the case where the current instruction under investigation is a phi and its operand is an llvm:intrinsic (like fmuladd)
                    // (this is just the reverse case of phi operand to an intrinsic - they both cycle with each other so both cases are equivalent)
                    for( const auto& user : intrin->users() )
                    {
                        if( auto phi = llvm::dyn_cast<llvm::PHINode>(user) )
                        {
                            if( Cyclebite::Graph::DNIDMap.at(phi)->isPredecessor(Cyclebite::Graph::DNIDMap.at(intrin)) )
                            {
                                // we have found a cycle between a phi and an intrinsic, likely indicating a reduction variable
                                seen.insert(intrin);
                                seen.insert(phi);
                                reductionOp = Cyclebite::Graph::DNIDMap.at(intrin);
                                reductionCandidates.insert(Cyclebite::Graph::DNIDMap.at(phi));
                            }
                        }
                    }
                }
                else if( auto phi = llvm::dyn_cast<llvm::PHINode>(use.get()) )
                {
                    // phis can lead to more function instructions (see 2DConv/PERFECT/ Task 12 [BBs102-108])
                    // thus we have to walk backward through the phi to its operands (that is, if we haven't found an RV candidate yet)
                    if( !seen.contains(phi) )
                    {
                        Q.push_back(phi);
                        seen.insert(phi);
                    }
                    // case found in optimized programs when an induction variable lives in a value (not the heap) and has a DFG cycle between an add and a phi node
                    // in order for this phi to be a reduction variable candidate, it must form a cycle with a binary operator
                    // we only expect this cycle to involve two nodes, thus a check of the phis users suffices to find the complete cycle
                    for( const auto& user : phi->users() )
                    {
                        if( auto bin = llvm::dyn_cast<llvm::BinaryOperator>(user) )
                        {
                            if( Cyclebite::Graph::DNIDMap.at(bin)->isPredecessor(Cyclebite::Graph::DNIDMap.at(phi)) )
                            {
                                // we have found a cycle between a binary op and a phi, likely indicating reduction variable
                                seen.insert(bin);
                                seen.insert(phi);
                                reductionOp = Cyclebite::Graph::DNIDMap.at(bin);
                                reductionCandidates.insert(Cyclebite::Graph::DNIDMap.at(phi));
                            }
                        }
                        // sometimes llvm will insert intrinsics into the code that hide reductions
                        // llvm.fmuladd is an example
                        // so look for an llvm intrinsic that can create a reduction
                        else if( const auto& intrin = llvm::dyn_cast<llvm::IntrinsicInst>(user) )
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
                else if( auto st = llvm::dyn_cast<llvm::StoreInst>(use.get()) )
                {
                    // shouldn't encounter this case, we started from the store and walked backwards
                    seen.insert(st);
                }
            }
            Q.pop_front();
        }
        // now find the induction variable(s) associated with each reduction variable candidate
        for( const auto& can : reductionCandidates )
        {
            // now that we have the candidate, we need to figure out how many dimensions it has
            // we do this by finding the number of cycles that the function group spans
            // we isolate the DFG subgraph that carries out the (possibly multi-dimensional) reduction by finding all nodes associated with a cycle
            set<shared_ptr<Graph::DataValue>> cycleNodes;
            {
                // we initiate the cycle finding territory to all function nodes
                set<shared_ptr<Graph::GraphNode>, Graph::p_GNCompare> graphNodes;
                set<shared_ptr<Graph::GraphEdge>, Graph::GECompare> graphEdges; 
                deque<shared_ptr<Graph::GraphNode>> Q;
                set<shared_ptr<Graph::GraphNode>> covered;
                Q.push_front(can);
                covered.insert(can);
                while( !Q.empty() )
                {
                    if( const auto& inst = dynamic_pointer_cast<Graph::Inst>(Q.front()) )
                    {
                        if( inst->isFunction() )
                        {
                            graphNodes.insert(inst);
                            for( const auto& pred : inst->getPredecessors() )
                            {
                                if( const auto& predInst = dynamic_pointer_cast<Graph::Inst>(pred->getSrc()) )
                                {
                                    if( predInst->isFunction() )
                                    {
                                        graphEdges.insert(pred);
                                        if( !covered.contains(predInst) )
                                        {
                                            Q.push_back(predInst);
                                            covered.insert(predInst);
                                        }
                                    }
                                }
                            }
                            for( const auto& succ : inst->getSuccessors() )
                            {
                                if( const auto& succInst = dynamic_pointer_cast<Graph::Inst>(succ->getSnk()) )
                                {
                                    if( succInst->isFunction() )
                                    {
                                        graphEdges.insert(succ);
                                        if( !covered.contains(succInst) )
                                        {
                                            Q.push_back(succInst);
                                            covered.insert(succInst);
                                        }
                                    }
                                }
                            }
                        }
                    }
                    Q.pop_front();
                }
                Graph::Graph cycleGraph(graphNodes, graphEdges);
                // we start the cycle hunt at the reduction node, then branch it out to its neighbors, each time finding new nodes in the cycle until no new nodes are eligible for searching
                Q.clear();
                covered.clear();
                Q.push_front(can);
                covered.insert(can);
                while( !Q.empty() )
                {
                    auto cycle = Graph::Dijkstras(cycleGraph, Q.front()->NID, Q.front()->NID);
                    if( !cycle.empty() )
                    {
                        if( const auto& dn = dynamic_pointer_cast<Graph::DataValue>(Q.front()) )
                        {
                            cycleNodes.insert(dn);
                        }
                        for( const auto& succ : Q.front()->getSuccessors() )
                        {
                            if( cycleGraph.find(succ->getSnk()) )
                            {
                                if( !covered.contains(succ->getSnk()) )
                                {
                                    Q.push_back(succ->getSnk());
                                    covered.insert(succ->getSnk());
                                }
                            }
                        }
                    }
                    Q.pop_front();
                }
            }
            // now that we have the cycle nodes, we put all the "addresses" into the RV (the phis)
            // the addresses are the phis that handle the reduction
            vector<shared_ptr<Graph::DataValue>> addresses;
            for( const auto& n : cycleNodes )
            {
                if( const auto& phi = llvm::dyn_cast<llvm::PHINode>(n->getVal()) )
                {
                    addresses.push_back(n);
                }
            }
            // next, we tak all the IVs involved in the reduction and assign those to the RV (this defines the dimensionality of the reduction)
            set<shared_ptr<Dimension>, DimensionSort> ivs;
            for( const auto& iv : vars )
            {
                for( const auto& n : cycleNodes )
                {
                    if( iv->getCycle()->find(n) )
                    {
                        ivs.insert(iv);
                        break;
                    }
                }
            }
            rvs.insert( make_shared<ReductionVariable>(ivs, addresses, reductionOp) );
        }
    }
    return rvs;
}