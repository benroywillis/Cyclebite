//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include <memory>

namespace Cyclebite::Grammar
{
    class Task;
    class Expression;
    void Export( const std::shared_ptr<Expression>& expr );
} // namespace Cyclebite::Grammar