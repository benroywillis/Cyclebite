//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "FunctionExpression.h"

using namespace std;
using namespace Cyclebite::Grammar;

string FunctionExpression::dump() const
{
    string expr = "";
    bool flip = false;
    if( !printedName )
    {
        flip = true;
        if( output )
        {
            expr += output->dump() + " <- ";
        }
    }
    printedName = true;
    expr += string(f->getName())+"( ";
    if( !args.empty() )
    {
        auto arg = args.begin();
        expr += (*arg++)->dump();
        while( arg != args.end() )
        {
            expr += ", ";
            expr += (*arg++)->dump();
        }
    }
    expr += " )";
    printedName = flip ? !printedName : printedName;
    return expr;
}

const llvm::Function* FunctionExpression::getFunction() const
{
    return f;
}