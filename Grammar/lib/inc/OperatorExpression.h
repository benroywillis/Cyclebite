//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include "Expression.h"
#include "Graph/inc/Operation.h"
#include <vector>

namespace Cyclebite::Grammar
{
    class OperatorExpression : public Expression
    {
    public:
        OperatorExpression(const std::shared_ptr<Task>& ta, Cyclebite::Graph::Operation o, const std::vector<std::shared_ptr<Symbol>>& a, const std::shared_ptr<Symbol>& out = nullptr );
        Cyclebite::Graph::Operation getOp() const;
        const std::vector<std::shared_ptr<Symbol>>& getArgs() const;
        std::string dump() const override;
        std::string dumpHalide( const std::map<std::shared_ptr<Dimension>, std::shared_ptr<ReductionVariable>>& dimToRV ) const override;
    private:
        Cyclebite::Graph::Operation op;
    protected:
        std::vector<std::shared_ptr<Symbol>> args;
    };
} // namespace Cyclebite::Grammar