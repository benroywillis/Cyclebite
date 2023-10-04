// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
#include "Cycle.h"
#include "IO.h"
#include <llvm/IR/Instructions.h>
#include <spdlog/spdlog.h>
#include "Util/Exceptions.h"
#include "Util/Annotate.h"
#include "Util/Print.h"
#include "Graph/inc/IO.h"

using namespace Cyclebite::Grammar;
using namespace Cyclebite::Graph;
using namespace std;

const llvm::BranchInst* Cycle::getIteratorInst() const
{
    return iteratorInst;
}

const set<shared_ptr<Cycle>>& Cycle::getChildren() const
{
    return children;
}

void Cycle::addChild( const shared_ptr<Cycle>& c)
{
    children.insert(c);
}

const set<shared_ptr<Cycle>>& Cycle::getParents() const
{
    return parents;
}

bool Cycle::find(const shared_ptr<DataValue>& v) const
{
    if( const auto& inst = dynamic_pointer_cast<Inst>(v) )
    {
        return find(inst);
    }
    return false;
}

bool Cycle::find(const shared_ptr<Inst>& n) const
{
    for( const auto& b : blocks )
    {
        if( b->instructions.find(n) != b->instructions.end() )
        {
            return true;
        }
    }
    return false;
}

bool Cycle::find(const shared_ptr<ControlBlock>& b) const
{
    return blocks.find(b) != blocks.end();
}

const set<shared_ptr<ControlBlock>, p_GNCompare>& Cycle::getBody() const
{
    return blocks;
}

void Cycle::addParent( const shared_ptr<Cycle>& p)
{
    parents.insert(p);
}

set<shared_ptr<Cycle>> Cyclebite::Grammar::ConstructCycles(const nlohmann::json& instanceJson, 
                                                            const nlohmann::json& kernelJson, 
                                                            const map<int64_t, llvm::BasicBlock*>& IDToBlock,
                                                            set<shared_ptr<Cycle>>& taskCycles)
{
    map<string, shared_ptr<Cycle>> idToCycle;
    set<shared_ptr<Cycle>> cycles;
    for (auto &[i, l] : kernelJson["Kernels"].items())
    {
        // first, construct set of ControlBlock objects within this cycle
        set<shared_ptr<ControlBlock>, p_GNCompare> blocks;
        for( const auto& id : l["Blocks"].get<vector<int64_t>>() )
        {
            blocks.insert( BBCBMap.at(IDToBlock.at(id)) );
        }
        // second, find the cmp inst that can either enter or exit the cycle
        const llvm::BranchInst* iteratorCmp = nullptr;
        for( const auto& b : blocks )
        {
            for( const auto& i : b->instructions )
            {
                if( i->isTerminator() )
                {
                    // for regular loops
                    if( auto br = llvm::dyn_cast<llvm::BranchInst>(i->getInst()) )
                    {
                        // br must have at least two targets
                        if( br->getNumSuccessors() > 1 )
                        {
                            // get its targets and see if they exit the cycle block set
                            // this check here is to make sure both targets are alive, if even one of them is dead we don't consider this br to be the iterator br
                            if( (BBCBMap.find(br->getSuccessor(0)) != BBCBMap.end()) && (BBCBMap.find(br->getSuccessor(1)) != BBCBMap.end()) )
                            {
                                auto dest0 = BBCBMap.at( br->getSuccessor(0) );
                                auto dest1 = BBCBMap.at( br->getSuccessor(1) );
                                if( (blocks.find(dest0) == blocks.end()) || (blocks.find(dest1) == blocks.end()) )
                                {
                                    // the destination of this edge is outside the cycle, thus it is an exit
                                    iteratorCmp = br;
                                    break;
                                }
                            }
                        }
                    }
                    else if( auto sel = llvm::dyn_cast<llvm::SelectInst>(i->getInst()) )
                    {
                        // do something
                        throw CyclebiteException("Cannot yet support select instructions for cycle iteration conditions!");
                    }
                    else if( auto ret = llvm::dyn_cast<llvm::ReturnInst>(i->getInst()) )
                    {
                        // if there is a function within the task, it's probably not the boundary of they cycle
                        // to confirm that this return exits the loop, we need to know what the successors of the block are
                        // corner case: a shared function's return will have successors outside the task
                        //  - for this reason we do away with the check (for now) 
                        /*if( BBCBMap.find(ret->getParent()) != BBCBMap.end() )
                        {
                            for( const auto& succ : BBCBMap.at(ret->getParent())->getSuccessors() )
                            {
                                if( blocks.find( succ->getSnk() ) == blocks.end() )
                                {
                                    PrintVal(ret);
                                    // we are in trouble. There' no way for us to know what the condition is that leads to this exit, thus we don't have a known method of finding the iteratorCondition of this cycle
                                    throw CyclebiteException("Cannot yet support function return instructions when finding cycle iteration condition!");
                                }
                            }
                        }*/
                        continue;
                    }
                }
            }
            if( iteratorCmp )
            {
                break;
            }
        }
        //  now that we have the block set and the iterator cmp, construct the cycle object
        if( iteratorCmp )
        {
            auto newCycle = make_shared<Cycle>(iteratorCmp, blocks);
            idToCycle[string(i)] = newCycle;
            // add this cycle to the parent cycles, if they exist
            for( const auto& c : l["Children"] )
            {
                string id = to_string(c.get<int64_t>());
                if( idToCycle.find( id ) != idToCycle.end() )
                {
                    auto child = idToCycle.find( id )->second;
                    child->addParent(newCycle);
                    newCycle->addChild(child);
                }
            }
            for( const auto& p : l["Parents"] )
            {
                string id = to_string(p.get<int64_t>());
                if( idToCycle.find(id) != idToCycle.end() )
                {
                    auto parent = idToCycle.find(id)->second;
                    parent->addChild(newCycle);
                    newCycle->addParent(parent);
                }
            }
            cycles.insert(newCycle);
            // if it maps to a task, add it to the taskCycle set
            if( instanceJson["Kernels"].find(string(i)) != instanceJson["Kernels"].end() )
            {
                taskCycles.insert(newCycle);
            }
        }
        else
        {
            throw CyclebiteException("Could not find iteratorCmp for a cycle!");
        }
    }
    return cycles;
}