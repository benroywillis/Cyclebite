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
    // structures for a basic block subgraph
    // a basic block subgraph is a basic block with its data flow annotated within
    // the basic block is a piece within the entire representation of the kernel that includes both its control flow and instruction-level DAG
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