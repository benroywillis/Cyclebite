//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "ConstantSymbol.h"

using namespace Cyclebite::Grammar;
using namespace std;


template <typename T>
string ConstantSymbol<T>::dump() const
{
    return to_string(bits);
}

template <typename T>
string ConstantSymbol<T>::dumpHalide( const map<shared_ptr<Dimension>, shared_ptr<ReductionVariable>>& dimToRV ) const
{
    return dump();
}