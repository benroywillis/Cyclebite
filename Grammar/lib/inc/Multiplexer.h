//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include "OperatorExpression.h"

namespace Cyclebite::Grammar
{
    class Multiplexer : public OperatorExpression
    {
    public:
        Multiplexer( const std::shared_ptr<Task>& ta, 
                     const std::shared_ptr<Cyclebite::Graph::DataValue>& cond, 
                     const std::vector<std::shared_ptr<Symbol>>& a, 
                     const std::shared_ptr<Symbol>& out = nullptr );
        const std::shared_ptr<Cyclebite::Graph::DataValue>& getCondition() const;
    private:
        std::shared_ptr<Cyclebite::Graph::DataValue> condition;
    };
} // namespace Cyclebite::Grammar