#include "Memory.h"
#include "Graph/inc/Inst.h"
#include "Graph/inc/IO.h"
#include "BasePointer.h"
#include "IndexVariable.h"
#include "Task.h"
#include "Util/Print.h"
#include "Util/Exceptions.h"
#include <deque>

using namespace std;
using namespace Cyclebite;
using namespace Cyclebite::Grammar;

set<GepNode, GepTreeSort> Cyclebite::Grammar::buildGepTree(const shared_ptr<Task>& t)
{
    // each load (that feeds the function group) and store (that remembers the result of the function group) must be explained by a collection
    set<const llvm::LoadInst*> lds;
    set<const llvm::StoreInst*> sts;
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
                        lds.insert( llvm::cast<llvm::LoadInst>(i->getVal()) );
                    }
                }
                else if( i->getOp() == Cyclebite::Graph::Operation::store )
                {
                    bool storesFunction = true;
                    const auto& pred = Graph::DNIDMap.at(llvm::cast<llvm::StoreInst>(i->getInst())->getValueOperand());
                    if( const auto& predInst = dynamic_pointer_cast<Graph::Inst>(pred) )
                    {
                        if( !predInst->isFunction() )
                        {
                            storesFunction = false;
                        }
                    }
                    if( storesFunction )
                    {
                        sts.insert( llvm::cast<llvm::StoreInst>(i->getVal()) );
                    }
                }
            }
        }
    }

    // this is a first pass at a gep tree builder
    // it currently walks the DFG, looking for all gep relations it finds
    // Challenge: 
    // - first: complete a DFS on the indices of the current gep
    // - second: complete a DFS on the pointer of the gep
    // - third: the child-most gep on the pointer side is the parent of the parent-most gep on the index side
    //   -> but are there cases that can break this?
    set<GepNode,GepTreeSort> gepTree;
    deque<const llvm::GetElementPtrInst*> gepQ;
    deque<const llvm::Value*> valQ;
    set<const llvm::Value*> covered;
    {
        deque<const llvm::Value*> ptrQ;
        set<const llvm::Value*> ptrCovered;
        // populate the gepQ with the highest-level geps from our captured loads and stored
        for( const auto& ld : lds )
        {
            ptrQ.push_front(ld->getPointerOperand());
            ptrCovered.insert(ld->getPointerOperand());
        }
        for( const auto& st : sts )
        {
            ptrQ.push_front(st->getPointerOperand());
            ptrCovered.insert(st->getPointerOperand());
        }
        while( !ptrQ.empty() )
        {
            if( const auto& gep = llvm::dyn_cast<llvm::GetElementPtrInst>(ptrQ.front()) )
            {
                gepQ.push_back(gep);
            }
            else
            {
                if( const auto& inst = llvm::dyn_cast<llvm::Instruction>(ptrQ.front()) )
                {
                    for( const auto& op : inst->operands() )
                    {
                        if( !ptrCovered.contains(op) )
                        {
                            ptrQ.push_back(op);
                            ptrCovered.insert(op);
                        }
                    }
                }
            }
            ptrQ.pop_front();
        }
    }
    // Data Structures
    // deque<llvm::GetElementPtrInst* gepQ - a queue of geps to investigate. The front of this queue is the gep whose child geps are currently being investigated
    // llvm::GetElementPtrInst* current    - the gep whose child geps are being found
    // Algorithm - a DFS
    // for each gep, we do two DFS
    // - the pointer first
    // - the indices second
    // when a new gepQ is encountered, we 
    while( !gepQ.empty() )
    {
        // check to see if the current Geps pointer DFS is done
        if( )
        {
            else if( const auto& ptrInst = llvm::dyn_cast<llvm::Instruction>(gepQ.front()->getPointerOperand()) )
            {
                for( const auto& op : ptrInst->operands() )
                {

                }
            }
        }
        else
        {
            // do its indices
        }
    }
    return gepTree;
}

/*
            else if( const auto& alloc = llvm::dyn_cast<llvm::AllocaInst>(Q.front()) )
            {
                // sometimes the alloc itself can be a base pointer
                for( const auto& bp : bps )
                {
                    if( bp->getNode()->getVal() == alloc )
                    {
#ifdef DEBUG
                        if( collBP )
                        {
                            if( collBP != bp )
                            {
                                throw CyclebiteException("Found multiple BPs for this collection!");
                            }
                        }
#endif
                        collBP = bp;
                        break;
                    }
                    // the base pointer may have been stored to this alloc after being dynamically allocated
                    // BasePointer::isOffset will tell us if this alloc stores the bp
                    else if( bp->isOffset(alloc) )
                    {
#ifdef DEBUG
                        if( collBP )
                        {
                            if( collBP != bp )
                            {
                                throw CyclebiteException("Found multiple BPs for this collection!");
                            }
                        }
#endif
                        collBP = bp;
                        break;
                    }
                }
            }
            else
            {
                for( const auto& op : Q.front()->operands() )
                {
                    if( const auto& arg = llvm::dyn_cast<llvm::Argument>(op) )
                    {
                        for( const auto& bp : bps )
                        {
                            if( bp->getNode()->getVal() == arg )
                            {
#ifdef DEBUG
                                if( collBP )
                                {
                                    if( collBP != bp )
                                    {
                                        throw CyclebiteException("Found multiple BPs for this collection!");
                                    }
                                }
#endif
                                collBP = bp;
                                break;
                            }
                        }
                    }
                    else if( const auto& opInst = llvm::dyn_cast<llvm::Instruction>(op) )
                    {
                        if( covered.find(opInst) == covered.end() )
                        {
                            Q.push_back(opInst);
                            covered.insert(opInst);
                        }
                    }
                }
            }
*/