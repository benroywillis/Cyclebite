//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include "GraphNode.h"
#include <llvm/IR/Value.h>

namespace Cyclebite::Graph
{
    class DataValue : public GraphNode
    {
    public:
        DataValue(const llvm::Value* val);
        ~DataValue() = default;
        const llvm::Value* getVal() const;
    private:
        const llvm::Value* v;
    };
} // namespace Cyclebite::Graph