//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include "ControlNode.h"

namespace Cyclebite::Graph
{
    class DataValue;
    class Inst;
    // Cyclebite's imitation of an llvm::BasicBlock
    // It holds DataValue's in their prescribed order that makes evaluation of the tasks straightforward (in Cyclebite-Template)
    class ControlBlock : public ControlNode
    {
    public:
        ControlBlock(std::shared_ptr<ControlNode> node, std::set<std::shared_ptr<Inst>, p_GNCompare> inst);
        const std::set<std::shared_ptr<Inst>, p_GNCompare>& getInstructions() const;
        const std::set<std::shared_ptr<Inst>, p_GNCompare> getNonDbgInsts() const;
        bool find( const std::shared_ptr<DataValue>& f ) const;
        uint64_t getFrequency() const;
        void addInstruction( const std::shared_ptr<Inst>& newInst );
    private:
        std::set<std::shared_ptr<Inst>, p_GNCompare> instructions;
    };
}; // namespace Cyclebite::Graph