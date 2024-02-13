//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include "Symbol.h"

namespace Cyclebite::Grammar
{
    template <typename T>
    class ConstantSymbol : public Symbol
    {
    public:
        ConstantSymbol(T b) : Symbol("const") , bits(b) {}
        std::string dump() const;
        std::string dumpHalide( const std::map<std::shared_ptr<Dimension>, std::shared_ptr<ReductionVariable>>& dimToRV ) const;
        T getVal() const;
    protected:
        T bits;
    };
    template class ConstantSymbol<int>;
    template class ConstantSymbol<int64_t>;
    template class ConstantSymbol<float>;
    template class ConstantSymbol<double>;
} // namespace Cyclebite::Grammar