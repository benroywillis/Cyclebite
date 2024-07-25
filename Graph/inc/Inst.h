//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include "DataValue.h"
#include "Operation.h"
#include <llvm/IR/Instruction.h>
#include <map>

namespace Cyclebite::Graph
{
    /// Data Node Category
    /// Used in CyclebiteTemplate to conveniently look up the category of each instruction
    enum class DNC
    {
        None,
        State,
        Function,
        Memory
    };
    /// Sits on top of llvm::Instruction*'s from the static program
    class Inst : public DataValue
    {
    public:
        std::shared_ptr<class ControlBlock> parent;
        Inst(const llvm::Instruction* inst, DNC t = DNC::None);
        const llvm::Instruction* getInst() const;
        /// Returns the Cyclebite::Graph::Operation of this instruction
        Operation getOp() const; 
        bool isState() const;
        bool isFunction() const;
        bool isMemory() const;
        bool isFunctionCall() const;
        bool isTerminator() const;
        bool isCaller() const;
        bool isBinaryOp() const;
        bool isCastOp() const;
        bool isComparator() const;
        void setColor(DNC color);
    private:
        /// An instruction that exists within the static program. This node represents that instruction
        const llvm::Instruction* inst;
        DNC type;
        Operation op;
    };
} // namespace Cyclebite::Graph