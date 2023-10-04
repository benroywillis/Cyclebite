//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include "Cycle.h"

namespace Cyclebite::Grammar
{
    std::set<std::shared_ptr<class Task>> getTasks(const nlohmann::json& instanceJson, 
                                                   const nlohmann::json& kernelJson, 
                                                   const std::map<int64_t, llvm::BasicBlock*>& IDToBlock);
    class Task
    {
    public:
        Task( const std::set<std::shared_ptr<Cycle>>& c) : cycles(c) {}
        const std::set<std::shared_ptr<Cycle>>& getCycles() const;
        const std::set<std::shared_ptr<Cycle>> getChildMostCycles() const;
        const std::set<std::shared_ptr<Cycle>> getParentMostCycles() const;
        bool find(const std::shared_ptr<Cyclebite::Graph::DataValue>& v) const;
        bool find(const std::shared_ptr<Cyclebite::Graph::Inst>& n) const;
        bool find(const std::shared_ptr<Cyclebite::Graph::ControlBlock>& b) const;
        bool find(const std::shared_ptr<Cycle>& c) const;
    private:
        std::set<std::shared_ptr<Cycle>> cycles;
    };
} // namespace Cyclebite::Grammar