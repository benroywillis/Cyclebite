//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "OperatorExpression.h"
#include "Util/Exceptions.h"
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
    switch(op)
    {
        // we only care about cast operations for now
        case Cyclebite::Graph::Operation::trunc        : expr += "Halide::trunc("; break;
        case Cyclebite::Graph::Operation::zext         : expr += "Halide::cast<uint64_t>("; break; // zero extend just adds bits to the front of an int
        case Cyclebite::Graph::Operation::sext         : expr += "Halide::cast<int64_t>("; break; // sign extend makes the extended integer signed
        case Cyclebite::Graph::Operation::fptoui       : expr += "Halide::cast<uint32_t>("; break;
        case Cyclebite::Graph::Operation::fptosi       : expr += "Halide::cast<int>("; break;
        case Cyclebite::Graph::Operation::uitofp       : expr += "Halide::cast<float>("; break;
        case Cyclebite::Graph::Operation::sitofp       : expr += "Halide::cast<float>("; break;
        case Cyclebite::Graph::Operation::fptrunc      : expr += "Halide::trunc("; break;
        case Cyclebite::Graph::Operation::fpext        : expr += "Halide::cast<double>("; break;
        case Cyclebite::Graph::Operation::ptrtoint     : expr += "Halide::cast<int>("; break;
        case Cyclebite::Graph::Operation::inttoptr     : expr += "Halide::cast<uint64_t>("; break;
        case Cyclebite::Graph::Operation::bitcast      : expr += "Halide::cast<void*>("; break;
        case Cyclebite::Graph::Operation::addrspacecast: expr += "Halide::cast<void*>("; break;
        case Cyclebite::Graph::Operation::fneg         : expr += "-"; break;
        default: throw CyclebiteException("Cannot yet handle a non-cast operator inside operator expressions yet! (Operation is a "+string(Cyclebite::Graph::OperationToString.at(op))+")");
    }
    if( !args.empty() )
    {
        auto arg = args.begin();
        if( symbol2Symbol.contains(*arg) )
        {
            if( const auto& childExpr = dynamic_pointer_cast<Expression>(symbol2Symbol.at(*arg)) )
            {
                expr += childExpr->dumpHalideReference(symbol2Symbol);
            }
            else
            {
                expr += symbol2Symbol.at(*arg)->dumpHalide(symbol2Symbol);
            }
        }
        else
        {
            if( const auto& childExpr = dynamic_pointer_cast<Expression>( *arg ) )
            {
                expr += childExpr->dumpHalideReference(symbol2Symbol);
            }
            else
            {
                expr += (*arg)->dumpHalide(symbol2Symbol);
            }
        }
        arg++;
        while( arg != args.end() )
        {
            expr += ", ";
            if( symbol2Symbol.contains(*arg) )
            {
                if( const auto& childExpr = dynamic_pointer_cast<Expression>(*arg) )
                {
                    expr += childExpr->dumpHalide(symbol2Symbol);
                }
                else
                {
                    expr += symbol2Symbol.at(*arg)->dumpHalide(symbol2Symbol);
                }
            }
            else
            {
                if( const auto& childExpr = dynamic_pointer_cast<Expression>(*arg) )
                {
                    expr += childExpr->dumpHalideReference(symbol2Symbol);
                }
                else
                {
                    expr += (*arg)->dumpHalide(symbol2Symbol);
                }
            }
            arg++;
        }
    }
    expr += ")";
    return expr;
}