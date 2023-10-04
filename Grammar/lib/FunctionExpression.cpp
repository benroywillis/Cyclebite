//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "FunctionExpression.h"

using namespace std;
using namespace Cyclebite::Grammar;

string FunctionExpression::dump() const
{
    string funcCall = string(f->getName())+"( ";
    if( !args.empty() )
    {
        auto arg = args.begin();
        funcCall += (*arg++)->dump();
        while( arg != args.end() )
        {
            funcCall += ", ";
            funcCall += (*arg++)->dump();
        }
    }
    funcCall += " )";
    return funcCall;
}

const llvm::Function* FunctionExpression::getFunction() const
{
    return f;
}