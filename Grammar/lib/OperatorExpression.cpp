//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "OperatorExpression.h"
#include <spdlog/spdlog.h>

using namespace std;
using namespace Cyclebite::Grammar;

OperatorExpression::OperatorExpression(Cyclebite::Graph::Operation o, const std::vector<std::shared_ptr<Symbol>>& a, const shared_ptr<Symbol>& out) : Expression( std::vector<std::shared_ptr<Symbol>>(),
                                                                                                                                                                  std::vector<Cyclebite::Graph::Operation>( {o} ), 
                                                                                                                                                                  out,
                                                                                                                                                                  Cyclebite::Graph::OperationToString.at(o) ),
                                                                                                                                                      op(o),
                                                                                                                                                      args(a) 
{
    // lets find all our inputs
    // symbols are hierarchically grouped, thus we need to search under the input list to find them all
    deque<shared_ptr<Symbol>> Q;
    set<shared_ptr<Symbol>> covered;
    for( const auto& ar : args )
    {
        Q.push_front(ar);
        covered.insert(ar);
    }
    while( !Q.empty() )
    {
        spdlog::info(Q.front()->dump());
        if( const auto& coll = dynamic_pointer_cast<Collection>(Q.front()) )
        {
            // collections present in the expression are always inputs
            inputs.insert(coll);
        }
        if( const auto& expr = dynamic_pointer_cast<Expression>(Q.front()) )
        {
            for( const auto& child : expr->getSymbols() )
            {
                if( !covered.contains(child) )
                {
                    Q.push_back(child);
                    covered.insert(child);
                }
            }
        }
        Q.pop_front();
    }
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