//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include "Counter.h"
#include "Symbol.h"

namespace Cyclebite::Grammar
{
    /// @brief InductionVariable is a Counter that must determine control
    class InductionVariable : public Counter, public Symbol
    {
    public:
        InductionVariable( const std::shared_ptr<Cyclebite::Graph::DataValue>& n, const std::shared_ptr<Cycle>& c, const llvm::Instruction* targetExit );
        std::string dump() const override; 
    };
    std::set<std::shared_ptr<InductionVariable>> getInductionVariables(const std::shared_ptr<Task>& t);
} // namespace Cyclebite::Grammar