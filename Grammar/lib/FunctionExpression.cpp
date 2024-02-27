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

string FunctionExpression::dumpHalide( const map<shared_ptr<Symbol>, shared_ptr<Symbol>>& symbol2Symbol ) const
{
    string expr = "";
    if( f->getName().find("llvm.fmuladd") != f->getName().npos )
    {
        // our op looks like llvm.fmuladd( arg0, arg1, arg2 )
        // the intrinsic needs to be converted in two ways
        // 1. the first two operands get multiplied ( arg0*arg1 )
        //    - the reduction arg (arg2) is implicit in the expression (from the +=), so it is omitted entirely
        // 2. the reduction dimension in the arg needs to be changed to the reduction variable
        if( symbol2Symbol.contains(args.front()) )
        {
            expr += symbol2Symbol.at(args.front())->dumpHalide(symbol2Symbol);
        }
        else
        {
            expr += args.front()->dumpHalide(symbol2Symbol);
        }
        expr += " * ";
        if( symbol2Symbol.contains( *next(args.begin()) ) )
        {
            expr += symbol2Symbol.at(*next(args.begin()))->dumpHalide(symbol2Symbol);
        }
        else
        {
            expr += (*next(args.begin()))->dumpHalide(symbol2Symbol);
        }
    }
    return expr;
}