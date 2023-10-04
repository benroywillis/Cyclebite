// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
#include "Collection.h"
#include "IndexVariable.h"
#include "Util/Exceptions.h"
#include "llvm/IR/Instructions.h"
#include "Graph/inc/IO.h"
#include "BasePointer.h"
#include "Util/Print.h"
#include <deque>

using namespace Cyclebite::Grammar;
using namespace Cyclebite::Graph;
using namespace std;

Collection::Collection(const std::set<std::shared_ptr<IndexVariable>>& v, const std::shared_ptr<BasePointer>& p ) :  Symbol("collection"), bp(p)
{
    // vars go in hierarchical order (parent-most is the first entry, child-most is the last entry)
    if( v.size() > 1 )
    {
        shared_ptr<IndexVariable> parentMost = nullptr;
        for( const auto& idx : v )
        {
            if( idx->getParent() == nullptr )
            {
                if( parentMost )
                {
                    PrintVal(parentMost->getNode()->getInst());
                    PrintVal(idx->getNode()->getInst());
                    throw CyclebiteException("Found more than one parent-most index variable in this collection!");
                }
                parentMost = idx;
            }
        }
        deque<shared_ptr<IndexVariable>> Q;
        set<shared_ptr<IndexVariable>> covered;
        Q.push_front(parentMost);
        covered.insert(parentMost);
        while( !Q.empty() )
        {
            vars.push_back(Q.front());
            for( const auto& c : Q.front()->getChildren() )
            {
                if( v.find(c) != v.end() )
                {
                    if( c->getBPs().find(p) != c->getBPs().end() )
                    {
                        if( covered.find(c) == covered.end() )
                        {
                            Q.push_back(c);
                            covered.insert(c);
                        }
                    }
                }
            }
            Q.pop_front();
        }
    }
    else
    {
        vars.push_back(*v.begin());
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

set<shared_ptr<Collection>> Cyclebite::Grammar::getCollections(const set<shared_ptr<IndexVariable>>& idxVars)
{
    // all collections that are forged in this method
    set<shared_ptr<Collection>> colls;

    // the idxVars already encode which base pointer(s) they map to, what their hierarchical order is, and which induction variable they may be using
    // but, this does not give us a clean, concise expression for the slabs of memory that are being indexed by the idxVars
    // thus, we describe these slabs of memory with "collections"

    // first step, find out how many slabs of memory there are (each one will get their own collection)
    // do this by going through the idxVars, finding their implicit hierarchies, and which base pointers they map to
    map<shared_ptr<BasePointer>, set<shared_ptr<IndexVariable>>> commonBPs;
    map<shared_ptr<BasePointer>, set<set<shared_ptr<IndexVariable>>>> varHierarchies;

    for( const auto& idx : idxVars )
    {
        for( const auto& bp : idx->getBPs() )
        {
            commonBPs[bp].insert(idx);
        }
    }
    // now sort the idxVars that map to the same BP into separate groups based on the hierarchies they form
    // this is a depth-first search: we are looking for all unique avenues from the root(s) of the idxVar tree to the leaves
    for( const auto& bp : commonBPs )
    {
        // remembers all non-leaf nodes that are loaded and have had a collection built for them
        set<shared_ptr<IndexVariable>> loadedAccounted;
        // search depth-first through all avenues from the root of the idxVar tree to the leaves
        set<shared_ptr<IndexVariable>> covered;
        for( const auto& idx : bp.second )
        {
            // we want to find the parent idxVar and walk through the tree from there
            if( covered.find(idx) != covered.end() )
            {
                continue;
            }
            else if( idx->getParent() )
            {
                continue;
            }
            deque<shared_ptr<IndexVariable>> Q;
            Q.push_front(idx);
            covered.insert(idx);
            while( !Q.empty() )
            {
                bool pop = true;
                if( Q.front()->getChildren().empty() )
                {
                    // we have hit a leaf, push the avenue we have observed
                    /*set<shared_ptr<IndexVariable>> newAvenue;
                    for( const auto& p : Q )
                    {
                        newAvenue.insert(p);
                    }
                    varHierarchies[bp.first].insert( newAvenue );*/
                    varHierarchies[bp.first].insert( set<shared_ptr<IndexVariable>>(Q.begin(), Q.end()) );
                }
                else
                {
                    if( Q.front()->isLoaded() )
                    {
                        if( loadedAccounted.find(Q.front()) == loadedAccounted.end() )
                        {
                            // there is an interesting corner case here where some base pointers are not offset by this particular combination of idxVars
                            // when a non-leaf idxVar is used to offset many base pointers, but it itself is used in a non-const-offset gep, it may only be offsetting a subset of these base pointers
                            // e.g.: StencilChain/Naive BB83 (OPFLAG=-O1)
                            // thus here, we implement a check to confirm this bp is offset by this combination of idxVars
                            // find the gep(s) that use the child-most idxVar but are not children of the child-most idxVar
                            set<const llvm::GetElementPtrInst*> targets;
                            for( const auto& gep : Q.front()->getGeps() )
                            {
                                bool isChild = false;
                                for( const auto& child : Q.front()->getChildren() )
                                {
                                    if( child->getNode() == gep )
                                    {
                                        isChild = true;
                                        break;
                                    }
                                }
                                if( !isChild )
                                {
                                    targets.insert(llvm::cast<llvm::GetElementPtrInst>(gep->getInst()));
                                }
                            }
                            // now that we have the non-child geps, confirm whether this bp is offset by them
                            bool hasOffset = false;
                            for( const auto& gep : targets )
                            {
                                if( bp.first->isOffset(gep) )
                                {
                                    hasOffset = true;
                                }
                            }
                            if( hasOffset )
                            {
                                varHierarchies[bp.first].insert( set<shared_ptr<IndexVariable>>(Q.begin(), Q.end()) );
                                loadedAccounted.insert(Q.front());
                            }
                        }
                    }
                    for( const auto& c : Q.front()->getChildren() )
                    {
                        if( covered.find(c) == covered.end() )
                        {
                            if( c->getBPs().find(bp.first) != c->getBPs().end() )
                            {
                                Q.push_front(c);
                                covered.insert(c);
                                pop = false;
                                break;
                            }
                        }
                    }
                }
                if( pop )
                {
                    Q.pop_front();
                }
            }
        }        
    }

    // now construct collections from the hierarchies that have been grouped
    for( const auto& bp : varHierarchies )
    {
        for( const auto& ivSet : bp.second )
        {
            colls.insert( make_shared<Collection>(ivSet, bp.first) );
            /*auto it = colls.insert( make_shared<Collection>(ivSet, bp.first) );
            PrintVal((*it.first)->getBP()->getNode()->getVal());
            for( const auto& iv : (*it.first)->getIndices() )
            {
                PrintVal(iv->getNode()->getInst());
            }
            cout << endl;*/
        }
    }
    /*map<shared_ptr<BasePointer>, set<shared_ptr<IndexVariable>>> varHierarchies;

    deque<shared_ptr<IndexVariable>> Q;
    set<shared_ptr<IndexVariable>> covered;
    for( const auto& idx : idxVars )
    {
        for( const auto& bp : idx->getBPs() )
        {
            varHierarchies[bp].insert(idx);
        }
    }

    // now just construct the collections
    for( const auto& bp : varHierarchies )
    {
        colls.insert( make_shared<Collection>(bp.second, bp.first) );
    }*/
    /*for( const auto& coll : colls )
    {
        PrintVal(coll->getBP()->getNode()->getVal());
        for( const auto& idx : coll->getIndices() )
        {
            PrintVal(idx->getNode()->getInst());
        }
    }*/
    return colls;
}