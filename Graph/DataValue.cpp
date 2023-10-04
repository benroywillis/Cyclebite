//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "DataValue.h"

using namespace std;
using namespace Cyclebite::Graph;

DataValue::DataValue(const llvm::Value* val) : v(val) {}

const llvm::Value* DataValue::getVal() const
{
    return v;
}