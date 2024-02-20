//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include <memory>
#include <map>
#include <vector>
#include <set>
#include <llvm/IR/Constant.h>

namespace Cyclebite::Grammar
{
    class Task;
    class Expression;
    class ConstantSymbol;
    /// Maps each constant parameter essential to the tasks to its symbol (this is used in the export operations to print the global at the top of the export)
    extern std::map<const llvm::Constant*, std::set<std::shared_ptr<ConstantSymbol>>> constants;
    void Export( const std::map<std::shared_ptr<Task>, std::vector<std::shared_ptr<Expression>>>& expr );
} // namespace Cyclebite::Grammar