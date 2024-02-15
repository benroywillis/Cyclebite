//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "OperatorExpression.h"
#include <spdlog/spdlog.h>

using namespace std;
using namespace Cyclebite::Grammar;

OperatorExpression::OperatorExpression( const shared_ptr<Task>& ta, 
                                        Cyclebite::Graph::Operation o, 
                                        const std::vector<std::shared_ptr<Symbol>>& a, 
                                        const shared_ptr<Symbol>& out) : Expression(ta, std::vector<std::shared_ptr<Symbol>>(),
                                                                                    std::vector<Cyclebite::Graph::Operation>( {o} ), 
                                                                                    out,
                                                                                    Cyclebite::Graph::OperationToString.at(o) ),
                                                                         op(o),
                                                                         args(a) 
{
    FindInputs(this);
}

Cyclebite::Graph::Operation OperatorExpression::getOp() const
{
    return op;
}

const vector<shared_ptr<Symbol>>& OperatorExpression::getArgs() const
{
    return args;
}

string OperatorExpression::dump() const
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
    expr += Graph::OperationToString.at(op) + string(" (");
    printedName = true;
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

string OperatorExpression::dumpHalide( const map<shared_ptr<Symbol>, shared_ptr<Symbol>>& symbol2Symbol ) const
{
    string expr = "";
    bool flip = false;
    if( !printedName )
    {
        flip = true;
        if( output )
        {
            expr += output->dumpHalide(symbol2Symbol) + " <- ";
        }
    }
    expr += Graph::OperationToString.at(op) + string(" (");
    printedName = true;
    if( !args.empty() )
    {
        auto arg = args.begin();
        expr += (*arg++)->dumpHalide(symbol2Symbol);
        while( arg != args.end() )
        {
            expr += ", ";
            expr += (*arg++)->dumpHalide(symbol2Symbol);
        }
    }
    expr += " )";
    printedName = flip ? !printedName : printedName;
    return expr;
}