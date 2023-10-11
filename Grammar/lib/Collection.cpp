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

Collection::Collection( const std::vector<std::shared_ptr<IndexVariable>>& v, const std::set<std::shared_ptr<BasePointer>>& p, const set<const llvm::Value*>& e ) : Symbol("collection"), vars(v), bps(p) 
{
    for( const auto& el : e )
    {
        if( const auto& ld = llvm::dyn_cast<llvm::LoadInst>(el) )
        {
            eps.insert(ld);
        }
        else if( const auto& st = llvm::dyn_cast<llvm::StoreInst>(el) )
        {
            eps.insert(st);
        }
        else
        {
            throw CyclebiteException("Collection element pointers can only be load and store instructions!");
        }
    }
}

uint32_t Collection::getNumDims() const
{
    return (uint32_t)vars.size();
}

const shared_ptr<IndexVariable>& Collection::operator[](unsigned i) const
{
    return vars[i];
}

const set<shared_ptr<BasePointer>>& Collection::getBPs() const
{
    return bps;
}

const vector<shared_ptr<IndexVariable>>& Collection::getIndices() const
{
    return vars;
}

const set<const llvm::Value*> Collection::getElementPointers() const
{
    return eps;
}

const llvm::LoadInst* Collection::getLoad() const
{
    set<const llvm::LoadInst*> lds;
    for( const auto& e : eps )
    {
        if( const auto& ld = llvm::dyn_cast<llvm::LoadInst>(e) )
        {
            lds.insert(ld);
        }
    }
    if( lds.size() > 1 )
    {
        for( const auto& bp : bps )
        {
            PrintVal(bp->getNode()->getVal());
        }
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
    // the starting points need to be the pointers of the ld/st (to ensure we only search through instructions that deal with memory)
    // the elements are the loads and stores themselves (that are used to construct the collections with)
    set<const llvm::Instruction*>  starts;
    set<const llvm::Instruction*>  elements;
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
                            elements.insert(i->getInst());
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
                                    elements.insert( i->getInst() );
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
        set<shared_ptr<BasePointer>> collBPs;
        deque<const llvm::Value*> Q;
        set<const llvm::Value*> covered;
        Q.push_front(st);
        covered.insert(st);
        // there is a corner case where static global structure will be offset and stored to 
        // thus we won't find a base pointer for the memory op
        // this flag remembers that so we can effectively check if the ld/st has had a base pointer mapped to it
        bool hasConstantPointer = false;
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
                    if( !covered.contains(op) )
                    {
                        Q.push_back(op);
                        covered.insert(op);
                    }
                }
            }
            else if( const auto& ld = llvm::dyn_cast<llvm::LoadInst>(Q.front()) )
            {
                // the pointer of this load may lead us to more geps
                if( !covered.contains(ld->getPointerOperand()) )
                {
                    Q.push_back(ld->getPointerOperand());
                    covered.insert(ld->getPointerOperand());
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
                        Q.push_back(op);
                        covered.insert(op);
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
                        collBPs.insert(bp);
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
                            Q.push_front(use);
                            covered.insert(use);
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
                        collBPs.insert(bp);
                    }
                }
            }
            else if( const auto& sto = llvm::dyn_cast<llvm::StoreInst>(Q.front()) )
            {
                // stores can put the bp into an alloc instruction
                // thus we walk backwards from the value operand of the store
                if( !covered.contains(sto->getValueOperand()) )
                {
                    Q.push_back(sto->getValueOperand());
                    covered.insert(sto->getValueOperand());
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
                    if( !covered.contains(op) )
                    {
                        Q.push_back(op);
                        covered.insert(op);
                    }
                }
            }
            else if( const auto& inst = llvm::dyn_cast<llvm::Instruction>(Q.front()) )
            {
                for( const auto& op : inst->operands() )
                {
                    if( !covered.contains(op) )
                    {
                        Q.push_back(op);
                        covered.insert(op);
                    }
                }
            }
            else if( const auto& con = llvm::dyn_cast<llvm::Constant>(Q.front()) )
            {
                // contants can appear in the search when a static global structure is being offset (ex. StencilChain filter weight array)
                // when we encounter them, we make a collection for them and put that pointer as the base pointer
                if( con->getType()->isPointerTy() || con->getType()->isArrayTy() )
                {
                    hasConstantPointer = true;
                }
            }
            else if( const auto& arg = llvm::dyn_cast<llvm::Argument>(Q.front()) )
            {
                // this may be a base pointer
                for( const auto& bp : bps )
                {
                    if( bp->getNode()->getVal() == arg )
                    {
                        collBPs.insert(bp);
                    }
                }
            }
            Q.pop_front();
        }
        if( hasConstantPointer && collBPs.empty() )
        {
            // we don't consider this ld/st for a collection
            continue;
        }
        else if( collBPs.empty() )
        {
            PrintVal(st);
            throw CyclebiteException("Could not find a base pointer for this memory op!");
        }
        set<const llvm::Value*> eps;
        for( const auto& e : elements )
        {
            if( const auto& ld = llvm::dyn_cast<llvm::LoadInst>(e) )
            {
                if( st == ld->getPointerOperand() )
                {
                    eps.insert(ld);
                }
            }
            else if( const auto& sto = llvm::dyn_cast<llvm::StoreInst>(e) )
            {
                if( st == sto->getPointerOperand() )
                {
                    eps.insert(sto);
                }
            }
            else
            {
                throw CyclebiteException("Should only find load and store instructions in the elements of collections!");
            }
        }
        vector<shared_ptr<IndexVariable>> varVec(vars.begin(), vars.end());
        colls.insert( make_shared<Collection>(varVec, collBPs, eps) );
    }
    return colls;
}