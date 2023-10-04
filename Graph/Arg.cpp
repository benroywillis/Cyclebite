// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
#include "Arg.h"

using namespace std;
using namespace Cyclebite::Graph;

const llvm::Argument* Arg::getArg() const
{
    return a;
}