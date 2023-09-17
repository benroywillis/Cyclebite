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
                    throw AtlasException("Found more than one parent-most index variables in this collection!");
                }
                parentMost = idx;
            }
        }
        deque<shared_ptr<IndexVariable>> Q;
        Q.push_front(parentMost);
        while( !Q.empty() )
        {
            vars.push_back(Q.front());
            for( const auto& c : Q.front()->getChildren() )
            {
                if( c->getBPs().find(p) != c->getBPs().end() )
                {
                    Q.push_back(c);
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

const llvm::Value* Collection::getElementPointer() const
{
    // the child-most index should connect us to the pointer that works with a load
    // this load should have successor(s) that are not in the memory group
    deque<const llvm::Instruction*> Q;
    set<const llvm::Instruction*> covered;
    // it is possible for our idxVar to be shared among many collections (e.g., when two base pointers are offset in the same way)
    // thus, we must pick our starting point based on which use of our child-most idxVar is associated with our base pointer
    if( vars.back()->getNode()->getSuccessors().size() > 1 )
    {
        for( const auto& succ : vars.back()->getNode()->getSuccessors() )
        {
            if( const auto& inst = dynamic_pointer_cast<Cyclebite::Graph::Inst>(succ->getSnk()) )
            {
                if( const auto& gep = llvm::dyn_cast<llvm::GetElementPtrInst>(inst->getInst()) )
                {
                    if( bp->isOffset(gep->getPointerOperand()) )
                    {
                        Q.push_front(gep);
                        covered.insert(gep);
                    }
                }
                else if( const auto& ld = llvm::dyn_cast<llvm::LoadInst>(inst->getInst()) )
                {
                    if( bp->isOffset(ld->getPointerOperand()) )
                    {
                        Q.push_front(ld);
                        covered.insert(ld);
                    }
                }
            }
        }
        if( Q.empty() )
        {
            PrintVal(bp->getNode()->getVal());
            for( const auto& idx : vars )
            {
                PrintVal(idx->getNode()->getInst());
            }
            throw AtlasException("Could not find element pointer of this collection!");
        }
    }
    else
    {
        Q.push_front(vars.back()->getNode()->getInst());
        covered.insert(vars.back()->getNode()->getInst());
    }
    while( !Q.empty() )
    {
        if( const auto& ld = llvm::dyn_cast<llvm::LoadInst>(Q.front()) )
        {
            // evaluate its successors, if it has no successors in the memory group this is our guy
            if( Cyclebite::Graph::DNIDMap.find(ld) != Cyclebite::Graph::DNIDMap.end() )
            {
                bool noMemory = true;
                for( const auto& succ : static_pointer_cast<Cyclebite::Graph::Inst>(Cyclebite::Graph::DNIDMap.at(ld))->getSuccessors() )
                {
                    if( const auto& nodeInst = dynamic_pointer_cast<Cyclebite::Graph::Inst>(succ->getSnk()) )
                    {
                        if( nodeInst->isMemory() )
                        {
                            noMemory = false;
                            break;
                        }
                    }
                }
                if( noMemory )
                {
                    return ld;
                }
            }
        }
        for( const auto& user : Q.front()->users() )
        {
            if( const auto& userInst = llvm::dyn_cast<llvm::Instruction>(user) )
            {
                if( covered.find(userInst) == covered.end() )
                {
                    Q.push_back(userInst);
                    covered.insert(userInst);
                }
            }
        }
        Q.pop_front();
    }
    return nullptr;
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