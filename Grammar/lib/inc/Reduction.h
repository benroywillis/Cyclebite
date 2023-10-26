//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include "Expression.h"
#include "ReductionVariable.h"
#include "Graph/inc/Inst.h"

namespace Cyclebite::Grammar
{
    class Reduction : public Expression
    {
    public:
        Reduction( const std::shared_ptr<Task>& ta, 
                   const std::shared_ptr<ReductionVariable>& var, 
                   const std::vector<std::shared_ptr<Symbol>>& in, 
                   const std::vector<Cyclebite::Graph::Operation>& o, 
                   const std::shared_ptr<Symbol>& out = nullptr );
        ~Reduction() = default;
        const std::shared_ptr<ReductionVariable>& getRV() const;
        const std::shared_ptr<Cycle>& getReductionCycle() const;
        bool isParallelReduction() const;
        std::string dump() const override;
    private:
        std::shared_ptr<ReductionVariable> rv;
    };
} // namespace Cyclebite::Grammar