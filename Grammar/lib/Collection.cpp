//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "Collection.h"
#include "IndexVariable.h"
#include "Util/Exceptions.h"
#include "llvm/IR/Instructions.h"
#include "Graph/inc/IO.h"
#include "BasePointer.h"
#include "Task.h"
#include "Util/Print.h"
#include <deque>

using namespace Cyclebite::Grammar;
using namespace Cyclebite::Graph;
using namespace std;

Collection::Collection(const std::vector<std::shared_ptr<IndexVariable>>& v, const std::shared_ptr<BasePointer>& p ) :  Symbol("collection"), vars(v), bp(p) {}

uint32_t Collection::getNumDims() const
{
    return (uint32_t)vars.size();
}

const shared_ptr<IndexVariable>& Collection::operator[](unsigned i) const
{
    return vars[i];
}

const shared_ptr<BasePointer>& Collection::getBP() const
{
    return bp;
}

const vector<shared_ptr<IndexVariable>>& Collection::getIndices() const
{
    return vars;
}

const set<const llvm::Value*> Collection::getElementPointers() const
{
    set<const llvm::Value*> eps;
    // the child-most index should connect us to the pointer that works with a load
    // this load should have successor(s) that are not in the memory group
    deque<const llvm::Instruction*> Q;
    set<const llvm::Instruction*> covered;
    // in order to find the starting point for our search, we need to walk the DFG until we find a gep or ld
    deque<shared_ptr<Graph::Inst>> varQ;
    // it is possible for our idxVar to be shared among many collections (e.g., when two base pointers are offset in the same way)
    // thus, we must pick our starting point based on which use of our child-most idxVar is associated with our base pointer
    varQ.push_front(vars.back()->getNode());
    while( !varQ.empty() )
    {
        if( varQ.front()->getOp() == Graph::Operation::load )
        {
            if( bp->isOffset( llvm::cast<llvm::LoadInst>(varQ.front()->getInst())->getPointerOperand() ) )
            {
                bool noMemory = true;
                for( const auto& succ : varQ.front()->getSuccessors() )
                {
                    if( const auto& nodeInst = dynamic_pointer_cast<Graph::Inst>(succ->getSnk()) )
                    {
                        // there is a corner case where a load can be immediately stored (DWT/PERFECT/BBID205 OPFLAG=-O1)
                        // in that case we will make an exception for store inst value operands
                        if( nodeInst->getOp() == Graph::Operation::store )
                        {
                            if( llvm::cast<llvm::StoreInst>(nodeInst->getInst())->getValueOperand() == varQ.front()->getInst() )
                            {
                                // make an exception
                            }
                            else
                            {
                                noMemory = false;
                                break;
                            }
                        }
                        else if( nodeInst->isMemory() )
                        {
                            noMemory = false;
                            break;
                        }
                    }
                }
                if( noMemory )
                {
                    eps.insert(varQ.front()->getInst());
                }
            }
        }
        else if( varQ.front()->getOp() == Graph::Operation::store )
        {
            if( bp->isOffset( llvm::cast<llvm::StoreInst>(varQ.front()->getInst())->getPointerOperand() ) )
            {
                eps.insert( varQ.front()->getInst() );
            }
        }
        else if( varQ.front()->getOp() == Graph::Operation::gep )
        {
            // a gep that is a child of the child-most var is not what we are looking for
            bool isChild = false;
            for( const auto& c : vars.back()->getChildren() )
            {
                if( c->getNode() == varQ.front() )
                {
                    isChild = true;
                    break;
                }
            }
            if( !isChild )
            {
                // then we determine if the base pointer of this collection is touched by this GEP
                if( bp->isOffset( llvm::cast<llvm::GetElementPtrInst>(varQ.front()->getInst())->getPointerOperand() ) )
                {
                    for( const auto& succ : varQ.front()->getSuccessors() )
                    {
                        if( const auto& inst = dynamic_pointer_cast<Graph::Inst>(succ->getSnk()) )
                        {
                            varQ.push_back(inst);
                        }
                    }
                }
                // corner case: sometimes a bp will be used to offset another bp (HistEq/PERFECT BBID118 OPFLAG=-O1)
                else if( llvm::isa<llvm::GetElementPtrInst>(vars.back()->getNode()->getInst()) )
                {
                    // vars.back() is actually a gep, meaning will have exactly one offset
                    // if this offset is an offset of the collection base pointer, it is a valid path to search
                    bool valid = false;
                    for( const auto& idx : llvm::cast<llvm::GetElementPtrInst>(varQ.front()->getInst())->indices() )
                    {
                        if( bp->isOffset(idx) )
                        {
                            valid = true;
                        }
                    }
                    if( valid )
                    {
                        for( const auto& succ : varQ.front()->getSuccessors() )
                        {
                            if( const auto& inst = dynamic_pointer_cast<Graph::Inst>(succ->getSnk()) )
                            {
                                varQ.push_back(inst);
                            }
                        }
                    }
                }
            }
        }
        else if( varQ.front()->isCastOp() )
        {
            for( const auto& succ : varQ.front()->getSuccessors() )
            {
                if( const auto& inst = dynamic_pointer_cast<Graph::Inst>(succ->getSnk()) )
                {
                    varQ.push_back(inst);
                }
            }
        }
        else if( varQ.front() == vars.back()->getNode() )
        {
            for( const auto& succ : varQ.front()->getSuccessors() )
            {
                if( const auto& inst = dynamic_pointer_cast<Graph::Inst>(succ->getSnk()) )
                {
                    varQ.push_back(inst);
                }
            }
        }
        varQ.pop_front();
    }
    if( eps.empty() )
    {
        PrintVal(bp->getNode()->getVal());
        for( const auto& var : vars )
        {
            PrintVal(var->getNode()->getInst());
        }
        throw CyclebiteException("Collection has no element pointers!");
    }
    return eps;
}

const llvm::LoadInst* Collection::getLoad() const
{
    set<const llvm::LoadInst*> lds;
    auto eps = getElementPointers();
    for( const auto& e : eps )
    {
        if( const auto& ld = llvm::dyn_cast<llvm::LoadInst>(e) )
        {
            lds.insert(ld);
        }
    }
    if( lds.size() > 1 )
    {
        PrintVal(bp->getNode()->getVal());
        for( const auto& var : vars )
        {
            PrintVal(var->getNode()->getInst());
        }
        for( const auto& ld : lds )
        {
            PrintVal(ld);
        }
        throw CyclebiteException("Collection maps to more than one load!");
    }
    return *lds.begin();
}

const set<const llvm::StoreInst*> Collection::getStores() const
{
    set<const llvm::StoreInst*> sts;
    auto eps = getElementPointers();
    for( const auto& e : eps )
    {
        if( const auto& st = llvm::dyn_cast<llvm::StoreInst>(e) )
        {
            sts.insert(st);
        }
    }
    return sts;
}

string Collection::dump() const
{
    string expr = name;
    if( !vars.empty() )
    {
        expr += "( ";
        auto v = vars.begin();
        expr += (*v)->dump();
        v = next(v);
        while( v != vars.end() )
        {
            expr += ", "+(*v)->dump();
            v = next(v);
        }
        expr += " )";
    }
    return expr;
}

set<shared_ptr<Collection>> Cyclebite::Grammar::getCollections(const shared_ptr<Task>& t, const set<shared_ptr<IndexVariable>>& idxVars)
{
    // all collections that are forged in this method
    set<shared_ptr<Collection>> colls;
    // holds all information necessary to construction collections
    // each base pointer contains groups of sorted index variables (from parent to child) that encode the hierarchy of idxVars for that collection
    // thus the <base pointer, idxVar hierarchy> pairs map 1:1 with collections
    map<shared_ptr<BasePointer>, set<vector<shared_ptr<IndexVariable>>>> varHierarchies;
    // base pointers help us end our search
    set<shared_ptr<BasePointer>> bps;
    for( const auto& idx : idxVars )
    {
        bps.insert(idx->getBPs().begin(), idx->getBPs().end());
    }

    // to build collections, we look for all loads and stores that must be explained by collections
    // these collections are later referred to when we build the task's function expression(s)
    set<const llvm::Instruction*>  starts;
    for( const auto& c : t->getCycles() )
    {
        for( const auto& b : c->getBody() )
        {
            for( const auto& i : b->instructions )
            {
                if( i->getOp() == Cyclebite::Graph::Operation::load )
                {
                    bool feedsFunction = true;
                    for( const auto& succ : i->getSuccessors() )
                    {
                        if( const auto& inst = dynamic_pointer_cast<Graph::Inst>(succ->getSnk()) )
                        {
                            if( !inst->isFunction() )
                            {
                                feedsFunction = false;
                                break;   
                            }
                        }
                    }
                    if( feedsFunction )
                    {
                        if( const auto& ptrInst = llvm::dyn_cast<llvm::Instruction>( llvm::cast<llvm::LoadInst>(i->getInst())->getPointerOperand() ) )
                        {
                            starts.insert( ptrInst );
                        }
                    }
                }
                else if( i->getOp() == Cyclebite::Graph::Operation::store )
                {
                    // value operand needs to be a function
                    if( DNIDMap.contains( llvm::cast<llvm::StoreInst>(i->getInst())->getValueOperand()) )
                    {
                        if( const auto& inst = dynamic_pointer_cast<Graph::Inst>(DNIDMap.at( llvm::cast<llvm::StoreInst>(i->getInst())->getValueOperand() )) )
                        {
                            if( inst->isFunction() )
                            {
                                if( const auto& ptrInst = llvm::dyn_cast<llvm::Instruction>( llvm::cast<llvm::StoreInst>(i->getInst())->getPointerOperand()) )
                                {
                                    starts.insert( ptrInst );
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // now that we have all instructions that need to be explained by collections, build collections for each one
    for( const auto& st : starts )
    {
        // st is a pointer operand from either a load or a store
        // we walk backward in the DFG from this point, collecting all information about the idxVars that lead to this pointer
        // because we walk backward, vars will be sorted in reverse-order, from child-most to parent-most 
        set<shared_ptr<IndexVariable>, idxVarHierarchySort> vars;
        shared_ptr<BasePointer> collBp = nullptr;
        deque<const llvm::Instruction*> Q;
        set<const llvm::Value*> covered;
        Q.push_front(st);
        covered.insert(st);
        while( !Q.empty() )
        {
            if( const auto& gep = llvm::dyn_cast<llvm::GetElementPtrInst>(Q.front()) )
            {
                // we have found a gep that we know is used by this collection
                // therefore, we can include all idxVars that are associated with it
                // to do this, we go through each index in the gep, map each one to a unique idx var, then push it to the set (that is sorted automatically)
                // find the idxVar(s) that map to this gep
                // the gep itself might be an idxVar so check it
                for( const auto& i : idxVars )
                {
                    if( i->isValueOrTransformedValue(gep) )
                    {
                        vars.insert(i);
                        break;
                    }
                }
                for( const auto& idx : gep->indices() )
                {
                    // find an idxVar for it
                    shared_ptr<IndexVariable> idxVar = nullptr;
                    for( const auto& i : idxVars )
                    {
                        if( i->isValueOrTransformedValue(idx) )
                        {
#ifdef DEBUG
                            if( idxVar )
                            {
                                if( idxVar != i )
                                {
                                    PrintVal(gep);
                                    PrintVal(idx);
                                    PrintVal(i->getNode()->getInst());
                                    PrintVal(idxVar->getNode()->getInst());
                                    throw CyclebiteException("Found more than one idxVar for this dimension of a gep!");
                                }
                            }
                            idxVar = i;
#else
                            idxVar = i;
                            break;
#endif
                        }
                    }
                    if( idxVar )
                    {
                        // now we push the found idxVar to the set
                        vars.insert(idxVar);
                    }
                }
                // once we are done absorbing all idxVars this gep has to offer, we must walk backward through its operands
                for( const auto& op : gep->operands() )
                {
                    if( const auto& opInst = llvm::dyn_cast<llvm::Instruction>(op) )
                    {
                        if( !covered.contains(opInst) )
                        {
                            Q.push_back(opInst);
                            covered.insert(opInst);
                        }
                    }
                    else if( const auto& arg = llvm::dyn_cast<llvm::Argument>(op) )
                    {
                        // this may be a base pointer
                        for( const auto& bp : bps )
                        {
                            if( bp->getNode()->getVal() == arg )
                            {
                                if( collBp )
                                {
                                    if( collBp != bp )
                                    {
                                        throw CyclebiteException("Found more than one possible base pointer for a collection!");
                                    }
                                }
                                collBp = bp;
                            }
                        }
                    }
                }
            }
            else if( const auto& ld = llvm::dyn_cast<llvm::LoadInst>(Q.front()) )
            {
                // the pointer of this load may lead us to more geps
                if( !covered.contains(ld->getPointerOperand()) )
                {
                    if( const auto& ptrInst = llvm::dyn_cast<llvm::Instruction>(ld->getPointerOperand()) )
                    {
                        Q.push_back(ptrInst);
                        covered.insert(ptrInst);
                    }
                    else if( const auto& arg = llvm::dyn_cast<llvm::Argument>(ld->getPointerOperand()) )
                    {
                        // this may be a base pointer
                        for( const auto& bp : bps )
                        {
                            if( bp->getNode()->getVal() == arg )
                            {
                                if( collBp )
                                {
                                    if( collBp != bp )
                                    {
                                        throw CyclebiteException("Found more than one possible base pointer for a collection!");
                                    }
                                }
                                collBp = bp;
                            }
                        }
                    }
                }
            }
            else if( const auto& cast = llvm::dyn_cast<llvm::CastInst>(Q.front()) )
            {
                // cast instructions can be used as shims between allocs and exact pointer types
                // thus, their operands are interesting
                for( const auto& op : cast->operands() )
                {
                    if( !covered.contains(op.get()) )
                    {
                        if( const auto& opInst = llvm::dyn_cast<llvm::Instruction>(op.get()) )
                        {
                            Q.push_back(opInst);
                            covered.insert(opInst);
                        }
                        else if( const auto& arg = llvm::dyn_cast<llvm::Argument>(op.get()) )
                        {
                            // this may be our base pointer
                            for( const auto& bp : bps )
                            {
                                if( bp->getNode()->getVal() == arg )
                                {
                                    // we have found a base pointer that may be the bp of this collection
                                    if( collBp )
                                    {
                                        if( collBp != bp )
                                        {
                                            throw CyclebiteException("Found more than one possible base pointer for a collection!");
                                        }
                                    }
                                    collBp = bp;
                                }
                            }
                        }
                    }
                }
            }
            else if( const auto& alloc = llvm::dyn_cast<llvm::AllocaInst>(Q.front()) )
            {
                // alloc instructions may be base pointers themselves or have a base pointer stored in them
                // if this alloc is a bp it will match a bp in the set
                bool found = false;
                for( const auto& bp : bps )
                {
                    if( bp->getNode()->getVal() == alloc )
                    {
                        if( collBp )
                        {
                            if( collBp != bp )
                            {
                                throw CyclebiteException("Found more than one possible base pointer for a collection!");
                            }
                        }
                        collBp = bp;
                        found = true;
                    }
                }
                // if this is alloc is not a bp, it may be storing one
                // thus we must walk forward to find the store that puts the bp into this alloc
                if( !found )
                {
                    for( const auto& use : alloc->users() )
                    {
                        if( !covered.contains(use) )
                        {
                            if( const auto& useInst = llvm::dyn_cast<llvm::Instruction>(use) )
                            {
                                Q.push_front(useInst);
                                covered.insert(useInst);
                            }
                        }
                    }
                }
            }
            else if( const auto& call = llvm::dyn_cast<llvm::CallBase>(Q.front()) )
            {
                // call bases may be base pointers
                for( const auto& bp : bps )
                {
                    if( bp->getNode()->getVal() == call )
                    {
                        if( collBp )
                        {
                            if( collBp != bp )
                            {
                                throw CyclebiteException("Found more than one possible base pointer for a collection!");
                            }
                        }
                        collBp = bp;
                    }
                }
            }
            else if( const auto& sto = llvm::dyn_cast<llvm::StoreInst>(Q.front()) )
            {
                // stores can put the bp into an alloc instruction
                // thus we walk backwards from the value operand of the store
                if( const auto& valueInst = llvm::dyn_cast<llvm::Instruction>(sto->getValueOperand()) )
                {
                    if( !covered.contains(valueInst) )
                    {
                        Q.push_back(valueInst);
                        covered.insert(valueInst);
                    }
                }
            }
            else if( const auto& bin = llvm::dyn_cast<llvm::BinaryOperator>(Q.front()) )
            {
                // might map to an idxVar
                shared_ptr<IndexVariable> idx = nullptr;
                for( const auto& i : idxVars )
                {
                    if( i->isValueOrTransformedValue(bin) )
                    {
#ifdef DEBUG
                        if( idx )
                        {
                            if( idx != i )
                            {
                                PrintVal(bin);
                                PrintVal(i->getNode()->getInst());
                                PrintVal(idx->getNode()->getInst());
                                throw CyclebiteException("Found more than one idxVar for this dimension of a gep!");
                            }
                        }
                        idx = i;
#else
                        idx = i;
                        break;
#endif
                    }
                }
                if( idx )
                {
                    vars.insert(idx);
                }
                // then push its operands into the queue for more investigation!
                for( const auto& op : bin->operands() )
                {
                    if( const auto& opInst = llvm::dyn_cast<llvm::Instruction>(op) )
                    {
                        if( !covered.contains(opInst) )
                        {
                            Q.push_back(opInst);
                            covered.insert(opInst);
                        }
                    }
                }
            }
            else if( const auto& inst = llvm::dyn_cast<llvm::Instruction>(Q.front()) )
            {
                for( const auto& op : inst->operands() )
                {
                    if( !covered.contains(op) )
                    {
                        if( const auto& opinst = llvm::dyn_cast<llvm::Instruction>(op) )
                        {
                            Q.push_back(opinst);
                            covered.insert(opinst);
                        }
                    }
                }
            }
            Q.pop_front();
        }
        if( !collBp )
        {
            PrintVal(st);
            throw CyclebiteException("Could not find a base pointer for this memory op!");
        }
        vector<shared_ptr<IndexVariable>> varVec(vars.begin(), vars.end());
        colls.insert( make_shared<Collection>(varVec, collBp) );
    }
    return colls;
}