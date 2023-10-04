// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
#include "Task.h"
#include <deque>

using namespace std;
using namespace Cyclebite::Grammar;

const set<shared_ptr<Cycle>>& Task::getCycles() const
{
    return cycles;
}

const set<shared_ptr<Cycle>> Task::getChildMostCycles() const
{
    set<shared_ptr<Cycle>> children;
    for( const auto& c : cycles )
    {
        if( c->getChildren().empty() )
        {
            children.insert(c);
        }
    }
    return children;
}

const set<shared_ptr<Cycle>> Task::getParentMostCycles() const
{
    set<shared_ptr<Cycle>> parents;
    for( const auto& c : cycles )
    {
        if( c->getParents().empty() )
        {
            parents.insert(c);
        }
    }
    return parents;
}

bool Task::find(const shared_ptr<Cyclebite::Graph::DataValue>& v) const
{
    if( const auto& inst = dynamic_pointer_cast<Cyclebite::Graph::Inst>(v) )
    {
        return find(inst);
    }
    return false;
}

bool Task::find(const shared_ptr<Cyclebite::Graph::Inst>& n) const
{
    for( const auto& c : cycles )
    {
        if( c->find(n) )
        {
            return true;
        }
    }
    return false;
}

bool Task::find(const shared_ptr<Cyclebite::Graph::ControlBlock>& b) const
{
    for( const auto& c : cycles )
    {
        if( c->find(b) )
        {
            return true;
        }
    }
    return false;
}

bool Task::find(const shared_ptr<Cycle>& c) const
{
    return cycles.find(c) != cycles.end();
}

set<shared_ptr<Task>> Cyclebite::Grammar::getTasks(const nlohmann::json& instanceJson, 
                                                    const nlohmann::json& kernelJson, 
                                                    const std::map<int64_t, llvm::BasicBlock*>& IDToBlock) {
    set<shared_ptr<Task>> tasks;
    // construct the cycles from the kernel file
    set<shared_ptr<Cycle>> taskCycles;
    auto cycles = ConstructCycles(instanceJson, kernelJson, IDToBlock, taskCycles);
    // group cycles together by their hierarchies
    set<set<shared_ptr<Cycle>>> candidates;
    set<shared_ptr<Cycle>> covered;
    for( const auto& cycle : taskCycles )
    {
        if( covered.find(cycle) != covered.end() )
        {
            continue;
        }
        set<shared_ptr<Cycle>> group;
        deque<shared_ptr<Cycle>> Q;
        Q.push_front(cycle);
        group.insert(cycle);
        while( !Q.empty() )
        {
            for( const auto& c : Q.front()->getChildren() )
            {
                if( covered.find(c) == covered.end() )
                {
                    Q.push_back(c);
                    covered.insert(c);
                    group.insert(c);
                }
            }
            for( const auto& p : Q.front()->getParents() )
            {
                if( covered.find(p) == covered.end() )
                {
                    Q.push_back(p);
                    covered.insert(p);
                    group.insert(p);
                }
            }
            Q.pop_front();
        }
        candidates.insert(group);
    }
    for( const auto& group : candidates )
    {
        tasks.insert( make_shared<Task>(group) );
    }
    return tasks;
}