//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include "Dimension.h"

namespace Cyclebite::Grammar
{
    class Task;
    /// @brief A Counter simply traverses an integer space, but it may not affect control
    class Counter : public Dimension
    {
    public:
        Counter( const std::shared_ptr<Cyclebite::Graph::DataValue>& n, const std::shared_ptr<Cycle>& c );
        ~Counter() = default;
        StridePattern getPattern() const;
        const PolySpace getSpace() const;
    protected:
        StridePattern pat;
        /// Warning: when the stride pattern is irregular (e.g., FFT shifting right each iteration), the stride pattern cannot be truly captured
        PolySpace space;
    };
    //std::set<std::shared_ptr<InductionVariable>> getCounters(const std::shared_ptr<Task>& t);
} // namespace Cyclebite::Grammar