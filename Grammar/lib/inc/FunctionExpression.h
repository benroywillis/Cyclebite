//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include "OperatorExpression.h"
#include <llvm/IR/Function.h>

namespace Cyclebite::Grammar
{
    class FunctionExpression : public OperatorExpression
    {
    public:
        FunctionExpression(const std::shared_ptr<Task>& ta, const llvm::Function* f, const std::vector<std::shared_ptr<Symbol>>& args, const std::shared_ptr<Symbol>& out = nullptr ) : OperatorExpression( ta, Cyclebite::Graph::Operation::call, args, out), f(f) {}
        std::string dump() const override;
        const llvm::Function* getFunction() const;
    private:
        const llvm::Function* f;
    };
} // namespace Cyclebite::Grammar