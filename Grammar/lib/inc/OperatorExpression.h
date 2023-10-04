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
        OperatorExpression(Cyclebite::Graph::Operation o, const std::vector<std::shared_ptr<Symbol>>& a) : Expression( std::vector<std::shared_ptr<Symbol>>(), 
                                                                                                           std::vector<Cyclebite::Graph::Operation>( {o} ), 
                                                                                                           Cyclebite::Graph::OperationToString.at(o) ), op(o), args(a) {}
        Cyclebite::Graph::Operation getOp() const;
        const std::vector<std::shared_ptr<Symbol>>& getArgs() const;
        std::string dump() const override;
    private:
        Cyclebite::Graph::Operation op;
    protected:
        std::vector<std::shared_ptr<Symbol>> args;
    };
} // namespace Cyclebite::Grammar