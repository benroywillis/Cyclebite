//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include "Collection.h"
#include "Expression.h"

namespace Cyclebite::Grammar
{
    class Function
    {
    public:
        std::string getName() const;
        const std::set<std::shared_ptr<Collection>>& getCollections() const;
        const std::shared_ptr<Expression>& getExpression() const;
        std::string dump() const; // (to halide, RTL, CUDA, etc)
    private:
        std::string name;
        std::set<std::shared_ptr<Collection>> collections;
        Expression expr;
    };
} // namespace Cyclebite::Grammar