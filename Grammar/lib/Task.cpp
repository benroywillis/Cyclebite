//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "Task.h"
#include "IO.h"
#include "Util/Exceptions.h"
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

uint64_t Task::getID() const
{
    return ID;
}

set<string> Task::getSourceFiles() const
{
    return sourceFiles;
}

void Task::addSourceFiles( set<string>& sources )
{
    sourceFiles.insert(sources.begin(), sources.end());
}

set<shared_ptr<Task>, TaskIDCompare> Cyclebite::Grammar::getTasks(const nlohmann::json& instanceJson, 
                                                    const nlohmann::json& kernelJson, 
                                                    const std::map<int64_t, const llvm::BasicBlock*>& IDToBlock)
{
    set<shared_ptr<Task>, TaskIDCompare> tasks;
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
        // id of the task is the parent-most cycle
        shared_ptr<Cycle> parentMost = nullptr;
        for( const auto& c : group )
        {
            if( c->getParents().empty() )
            {
#ifdef DEBUG
                if( parentMost )
                {
                    throw CyclebiteException("Already found parent-most cycle! Cannot determine ID of task.");
                }
#endif
                parentMost = c;
            }
        }
        auto newTask = make_shared<Task>(group, parentMost->getID());
        set<string> sources;
        for( const auto& c : newTask->getCycles() )
        {
            for( const auto& b : c->getBody() )
            {
                for( const auto id : b->originalBlocks )
                {
                    if( blockToSource.contains(id) )
                    {
                        sources.insert( blockToSource.at(id).first );
                    }
                }
            }
        }
        newTask->addSourceFiles(sources);
        for( auto c : newTask->getCycles() )
        {
            c->addTask(newTask);
        }
        tasks.insert( newTask );
    }
    // add producer-consumer relationships to tasks
    for( auto& prod : tasks )
    {
        if( instanceJson.contains("Communication") )
        {
            if( instanceJson["Communication"].contains(to_string(prod->getID())) )
            {
                for( auto& cons : instanceJson["Communication"][to_string(prod->getID())].get<vector<uint64_t>>() )
                {
                    auto consTask = *tasks.find(cons);
                    auto prodConEdge = make_shared<Cyclebite::Graph::GraphEdge>(prod, consTask);
                    prod->addSuccessor(prodConEdge);
                    consTask->addPredecessor(prodConEdge);
                }
            }
        }
    }

    return tasks;
}