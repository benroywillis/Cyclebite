//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include "Graph/inc/ControlBlock.h"
#include "Graph/inc/DataGraph.h"
#include <llvm/IR/Instructions.h>
#include <nlohmann/json.hpp>
#include "Graph/inc/Inst.h"

namespace Cyclebite::Grammar
{
    class Task;
    std::set<std::shared_ptr<class Cycle>> ConstructCycles(const nlohmann::json& instanceJson, 
                                                           const nlohmann::json& kernelJson, 
                                                           const std::map<int64_t, const llvm::BasicBlock*>& IDToBlock,
                                                           std::set<std::shared_ptr<Cycle>>& taskCycles);
    class Cycle
    {
    public:
        Cycle() = default;
        Cycle(const std::set<const llvm::Instruction*> e, const std::set<std::shared_ptr<Cyclebite::Graph::ControlBlock>, Cyclebite::Graph::p_GNCompare>& b, uint64_t id = 0) : ID(id), exits(e), blocks(b) {}
        /// @brief returns all (live) llvm::instructions (terminators) that have a destination outside the blocks of this cycle
        ///
        /// Cycle exits are strictly defined as edges that leave the blocks of the cycle - this could mean the edge goes to non-cycle code, a parent cycle, or a child cycle. This method returns all llvm::instructions (terminators) that have a (live) destination outside the current cycle
        const std::set<const llvm::Instruction*>& getExits() const;
        /// @brief returns only the llvm::instructions (terminators) that have a (live) destination in either non-cycle code or a parent cycle
        ///
        /// if the exit edge goes to a child cycle, it will not be included in the returned set. Only the llvm::instructions (terminators) that have a (live) destination in a parent cycle or non-cycle code will be returned.
        const std::set<const llvm::Instruction*> getNonChildExits() const;
        /// @brief Only returns llvm::instructions (terminators) that have a (live) destination outside the parent cycles of this cycle
        const std::set<const llvm::Instruction*> getNonParentExits() const;
        /// @brief Returns all llvm::instructions (terminators) that have a (live) destination in non-cycle code
        const std::set<const llvm::Instruction*> getNonCycleExits() const;
        const std::set<std::shared_ptr<class Cycle>>& getChildren() const;
        const std::set<std::shared_ptr<class Cycle>>& getParents() const;
        const std::shared_ptr<Task>& getTask() const;
        const std::set<std::shared_ptr<Cyclebite::Graph::ControlBlock>, Cyclebite::Graph::p_GNCompare>& getBody() const;
        /// Returns true only if the input parameter is within the cycle blocks. Does not explore child or parent cycles.
        bool find(const std::shared_ptr<Cyclebite::Graph::DataValue>& v) const;
        /// Returns true only if the input parameter is within the cycle blocks. Does not explore child or parent cycles.
        bool find(const std::shared_ptr<Cyclebite::Graph::Inst>& n) const;
        /// Returns true only if the input parameter is a cycle block. Does not explore child or parent cycles.
        bool find(const std::shared_ptr<Cyclebite::Graph::ControlBlock>& b) const;
        void addChild(const std::shared_ptr<class Cycle>& c);
        void addParent(const std::shared_ptr<class Cycle>& p);
        void addTask(const std::shared_ptr<Task>& t);
        uint64_t getID() const;
    private:
        uint64_t ID;
        std::shared_ptr<Task> task;
        std::set<const llvm::Instruction*> exits;
        std::set<std::shared_ptr<Cyclebite::Graph::ControlBlock>, Cyclebite::Graph::p_GNCompare> blocks;
        std::set<std::shared_ptr<class Cycle>> children;
        std::set<std::shared_ptr<class Cycle>> parents;
    };
} // namespace Cyclebite::Grammar