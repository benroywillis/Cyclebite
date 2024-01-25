//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include "Graph/inc/DataGraph.h"
#include <llvm/Support/SourceMgr.h>
#include <llvm/IR/Instruction.h>
#include <cstdint>
#include <set>
#include <map>
#include <memory>
#include <string>

namespace Cyclebite::Grammar
{
    enum class OpColor
    {
        Red,
        Blue,
        Green
    };

    struct NodeColor
    {
        const llvm::Instruction *inst;
        std::set<OpColor> colors;
        NodeColor() {}
        NodeColor(const llvm::Instruction *Inst, OpColor color)
        {
            inst = Inst;
            colors.insert(color);
        }
        NodeColor(const llvm::Instruction *Inst, std::set<OpColor> Colors)
        {
            inst = Inst;
            colors = Colors;
        }
    };

    struct NCCompare
    {
        using is_transparent = void;
        bool operator()(const std::shared_ptr<NodeColor> &lhs, const std::shared_ptr<NodeColor> &rhs) const
        {
            return lhs->inst < rhs->inst;
        }
    };
    class Task;
    struct TaskIDCompare;
    void colorNodes( const std::set<std::shared_ptr<Task>, TaskIDCompare>& tasks );
} // namespace Cyclebite::Grammar