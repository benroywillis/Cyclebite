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
#include "Expression.h"
#include "Util/Print.h"
#include "IO.h"
#include <deque>

using namespace Cyclebite::Grammar;
using namespace Cyclebite::Graph;
using namespace std;

Collection::Collection( const std::vector<std::shared_ptr<IndexVariable>>& v, const std::shared_ptr<BasePointer>& p, const set<const llvm::Value*>& e ) : Symbol("collection"), vars(v), indexBP(p) 
{
    for( const auto& var : vars )
    {
        offsetBPs.insert(var->getOffsetBPs().begin(), var->getOffsetBPs().end());
    }
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

string Collection::getBoundedName() const
{
    return name+"_bounded";
}

const set<shared_ptr<Dimension>, DimensionSort> Collection::getDimensions() const
{
    set<shared_ptr<Dimension>, DimensionSort> dims;
    for( const auto& var : vars )
    {
        dims.insert(var->getDimensions().begin(), var->getDimensions().end());
    }
    return dims;
}

uint32_t Collection::getNumDims() const
{
    return (uint32_t)getDimensions().size();
}

vector<PolySpace> Collection::getPolyhedralSpace() const
{
    vector<PolySpace> dims;
    for( const auto& dim : getDimensions() )
    {
        if( const auto& count = dynamic_pointer_cast<Counter>(dim) )
        {
            dims.push_back(count->getSpace());
        }
    }
    return dims;
}

const shared_ptr<IndexVariable>& Collection::operator[](unsigned i) const
{
    return vars[i];
}

const shared_ptr<BasePointer>& Collection::getBP() const
{
    return indexBP;
}

const set<shared_ptr<BasePointer>>& Collection::getOffsetBPs() const
{
    return offsetBPs;
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
        Cyclebite::Util::PrintVal(indexBP->getNode()->getVal());
        for( const auto& var : vars )
        {
            Cyclebite::Util::PrintVal(var->getNode()->getVal());
        }
        for( const auto& ld : lds )
        {
            Cyclebite::Util::PrintVal(ld);
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

string Collection::dumpHalide( const map<shared_ptr<Symbol>, shared_ptr<Symbol>>& symbol2Symbol ) const
{
    string expr = "";
    for( const auto& s : symbol2Symbol )
    {
        if( s.first.get() == this )
        {
            // check to see if it is an expression, then we just dump a reference to it
            if( const auto& e = dynamic_pointer_cast<Expression>(s.second) )
            {
                expr += e->getName();
            }
            else if( const auto& c = dynamic_pointer_cast<Collection>(s.second) )
            {
                // dump the mapped collection method
                expr += s.second->dumpHalide(symbol2Symbol);
                return expr;
            }
        }
    }
    if( expr.empty() )
    {
        expr += name;
    }
    if( !vars.empty() )
    {
        expr += "(";
        auto v = vars.begin();
        // this loop waits until a valid (dimension > -1) idxVar is found to start printing
        string symbolText = "";
        while( symbolText.empty() && (v != vars.end()) )
        {
            symbolText = (*v)->dumpHalide(symbol2Symbol);
            if( !symbolText.empty() )
            {
                expr += symbolText;
            }
            v = next(v);
        }
        symbolText.clear();
        while ( v != vars.end() )
        {
            symbolText = (*v)->dumpHalide(symbol2Symbol);
            if( !symbolText.empty() )
            {
                expr += ", "+symbolText;
            }
            v = next(v);
        }
        expr += ")";
    }
    return expr;
}

set<shared_ptr<IndexVariable>> Collection::overlaps( const shared_ptr<Collection>& coll ) const
{
    set<shared_ptr<IndexVariable>> overlapDims;
    // if the collections do not touch the same bp, we can trivially say they do not overlap
    if( indexBP != coll->getBP() )
    {
        return overlapDims;
    }
    for( const auto& var0 : vars )
    {
        for( const auto& var1 : coll->getIndices() )
        {
            if( var0->overlaps(var1) )
            {
                overlapDims.insert(var0);
            }
        }
    }
    return overlapDims;
}

set<shared_ptr<Collection>> Cyclebite::Grammar::getCollections(const shared_ptr<Task>& t, const set<shared_ptr<BasePointer>>& bps, const set<shared_ptr<IndexVariable>>& idxVars)
{
    // all collections that are forged in this method
    set<shared_ptr<Collection>> colls;
    // holds all information necessary to construction collections
    // each base pointer contains groups of sorted index variables (from parent to child) that encode the hierarchy of idxVars for that collection
    // thus the <base pointer, idxVar hierarchy> pairs map 1:1 with collections
    map<shared_ptr<BasePointer>, set<vector<shared_ptr<IndexVariable>>>> varHierarchies;
    // to build collections, we look for all loads and stores that must be explained by collections
    // these collections are later referred to when we build the task's function expression(s)
    // the starting points need to be the pointers of the ld/st (to ensure we only search through instructions that deal with memory)
    // the elements are the loads and stores themselves (that are used to construct the collections with)
    set<const llvm::Value*>  starts;
    // stores the dereferenced collection values
    // an "element" is a value that came from a collection
    set<const llvm::Instruction*>  elements;
    for( const auto& c : t->getCycles() )
    {
        for( const auto& b : c->getBody() )
        {
            for( const auto& i : b->getInstructions() )
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
                        else if( const auto& arg = llvm::dyn_cast<llvm::Argument>( llvm::cast<llvm::LoadInst>(i->getInst())->getPointerOperand() ) )
                        {
                            // confirm this is a significant mem inst and push it into the starts
                            if( SignificantMemInst.contains(i) )
                            {
                                starts.insert(i->getInst());
                                elements.insert(i->getInst());
                            }
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
                                starts.insert( llvm::cast<llvm::StoreInst>(i->getInst())->getPointerOperand() );
                                elements.insert( i->getInst() );
                            }
                        }
                        else
                        {
                            spdlog::warn("The value of a task store was not an instruction!");
                        }
                    }
                    else
                    {
                        Cyclebite::Util::PrintVal( llvm::cast<llvm::StoreInst>(i->getInst())->getValueOperand() );
                        Cyclebite::Util::PrintVal(i->getInst());
                        spdlog::warn("Could not find the value of a task store in the DNID map!");
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
        // we can find multiple base pointers for this start along the walk (they can either be the base pointer being indexed or base pointers used to index the underlying memory)
        // we resolve which bp is the indexed base pointer and assign that one to the collection
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
                for( const auto& idx : gep->indices() )
                {
                    // find an idxVar for it
                    shared_ptr<IndexVariable> idxVar = nullptr;
                    for( const auto& i : idxVars )
                    {
                        if( i->isValueOrTransformedValue(gep, idx) )
                        {
#ifdef DEBUG
                            if( idxVar )
                            {
                                if( idxVar != i )
                                {
                                    Cyclebite::Util::PrintVal(gep);
                                    Cyclebite::Util::PrintVal(idx);
                                    Cyclebite::Util::PrintVal(i->getNode()->getVal());
                                    Cyclebite::Util::PrintVal(idxVar->getNode()->getVal());
                                    throw CyclebiteException("Found more than one idxVar for this dimension of a gep!");
                                }
                            }
                            idxVar = i;
#else
                            idxVar = i;
                            break;
#endif
                        }
                        else if( const auto& inst = llvm::dyn_cast<llvm::Instruction>(idx) )
                        {
                            if( i->isValueOrTransformedValue(inst, inst) )
                            {
#ifdef DEBUG
                                if( idxVar )
                                {
                                    if( idxVar != i )
                                    {
                                        Cyclebite::Util::PrintVal(gep);
                                        Cyclebite::Util::PrintVal(idx);
                                        Cyclebite::Util::PrintVal(i->getNode()->getVal());
                                        Cyclebite::Util::PrintVal(idxVar->getNode()->getVal());
                                        throw CyclebiteException("Found more than one idxVar for this dimension of a gep!");
                                    }
                                }
                                idxVar = i;
    #else
                                idxVar = i;
                                break;
    #endif
                                idxVar = i;
                            }
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
                // load instructions may be base pointers (if they are being loaded from a user-defined structure)
                bool found = false;
                for( const auto& bp : bps )
                {
                    if( bp->getNode()->getVal() == ld )
                    {
                        collBPs.insert(bp);
                        found = true;
                    }
                }
                // if this is load is not a bp, it may be storing one
                // thus we must walk forward to find the store that puts the bp into this alloc
                if( !found )
                {
                    for( const auto& use : ld->users() )
                    {
                        if( !covered.contains(use) )
                        {
                            Q.push_front(use);
                            covered.insert(use);
                        }
                    }
                }
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
                    if( i->isValueOrTransformedValue(bin, bin) )
                    {
#ifdef DEBUG
                        if( idx )
                        {
                            if( idx != i )
                            {
                                Cyclebite::Util::PrintVal(bin);
                                Cyclebite::Util::PrintVal(i->getNode()->getVal());
                                Cyclebite::Util::PrintVal(idx->getNode()->getVal());
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
            else if( const auto& glob = llvm::dyn_cast<llvm::GlobalValue>(Q.front()) )
            {
                // may be a base pointer, or may lead us to one
                bool foundBP = false;
                for( const auto& bp : bps )
                {
                    if( bp->getNode()->getVal() == glob )
                    {
                        collBPs.insert(bp);
                        foundBP = true;
                    }
                }
                if( !foundBP )
                {
                    if( glob->getType()->isPointerTy() || glob->getType()->isArrayTy() )
                    {
                        hasConstantPointer = true;
                    }
                }
                for( const auto& user : glob->users() )
                {
                    if( !covered.contains(user) )
                    {
                        Q.push_back(user);
                        covered.insert(user);
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
            Cyclebite::Util::PrintVal(st);
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
                // accomodates the case when a load uses a function arg directly
                else if( st == ld )
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
        // now we have the information we need to build collection(s) from the base pointers and idxVars we found
        /*for( const auto& potentialIndexBP : collBPs )
        {
            // this set holds the idxVars whose indexBPs map to the potentialIndexBP
            set<shared_ptr<IndexVariable>,idxVarHierarchySort> commonIdxs;
            // we need to check to see if the vars we acquired explain the entire idx hierarchy - we do this by counting the underlying nodes we map to
            set<shared_ptr<Graph::DataValue>> totalNodes;
            for( const auto& idx : vars )
            {
                if( idx->getBP() == potentialIndexBP )
                {
                    totalNodes.insert(idx->getNode());
                    commonIdxs.insert(idx);
                }
            }
            if( (!commonIdxs.empty()) && (commonIdxs.size() == totalNodes.size()) )
            {
                // we have a complete hierarchy, build a collection with this information
                vector<shared_ptr<IndexVariable>> varVec(commonIdxs.begin(), commonIdxs.end());
                colls.insert( make_shared<Collection>( varVec, potentialIndexBP, eps ) );
            }
        }*/
        // now we have a set of index variables and a set of base pointers that form collections
        // to do this we simply construct a collection for each base pointer, and assign it all the vars we found for it
        vector<shared_ptr<IndexVariable>> varVec(vars.begin(), vars.end());
        shared_ptr<BasePointer> commonBP = nullptr;
        for( const auto& bp : collBPs )
        {
            // there is a catch here: the bp may be used by the vars to offset true indexBP
            // it is also possible for those offset BPs to be indexBPs themselves
            // we are not exactly hurt by having too many Collections, so for now we construct them all haphazardly
            colls.insert( make_shared<Collection>(varVec, bp, eps) );
        }
    }
    return colls;
}