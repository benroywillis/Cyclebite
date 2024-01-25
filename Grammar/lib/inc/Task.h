//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include "Cycle.h"
#include "Graph/inc/GraphNode.h"

namespace Cyclebite::Grammar
{
    class Cycle;
    std::set<std::shared_ptr<class Task>, struct TaskIDCompare> getTasks(const nlohmann::json& instanceJson, 
                                                   const nlohmann::json& kernelJson, 
                                                   const std::map<int64_t, const llvm::BasicBlock*>& IDToBlock);
    class Task : public Cyclebite::Graph::GraphNode
    {
    public:
        Task( const std::set<std::shared_ptr<Cycle>>& c, uint64_t id = 0 ) : ID(id), cycles(c) {}
        const std::set<std::shared_ptr<Cycle>>& getCycles() const;
        const std::set<std::shared_ptr<Cycle>> getChildMostCycles() const;
        const std::set<std::shared_ptr<Cycle>> getParentMostCycles() const;
        bool find(const std::shared_ptr<Cyclebite::Graph::DataValue>& v) const;
        bool find(const std::shared_ptr<Cyclebite::Graph::Inst>& n) const;
        bool find(const std::shared_ptr<Cyclebite::Graph::ControlBlock>& b) const;
        bool find(const std::shared_ptr<Cycle>& c) const;
        uint64_t getID() const;
        std::set<std::string> getSourceFiles() const;
        void addSourceFiles( std::set<std::string>& sources );
    private:
        uint64_t ID;
        std::set<std::shared_ptr<Cycle>> cycles;
        std::set<std::string> sourceFiles;
    };

    struct TaskIDCompare
    {
        using is_transparent = void;
        bool operator()( const std::shared_ptr<Task>& lhs, const std::shared_ptr<Task>& rhs ) const
        {
            return lhs->getID() < rhs->getID();
        }
        bool operator()( const std::shared_ptr<Task>& lhs, uint64_t rhs ) const
        {
            return lhs->getID() < rhs;
        }
        bool operator()( uint64_t lhs, const std::shared_ptr<Task>& rhs ) const
        {
            return lhs < rhs->getID();
        }
    };
} // namespace Cyclebite::Grammar