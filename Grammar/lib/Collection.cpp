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
    // it is possible for our idxVar to be shared among many collections (e.g., when two base pointers are offset in the same way)
    // thus, we must pick our starting point based on which use of our child-most idxVar is associated with our base pointer
    // in order to find the starting point for our search, we need to walk the DFG until we find a gep or ld
    deque<shared_ptr<Graph::Inst>> varQ;
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
            // a gep that is the child of the child-most var is not what we are looking for
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