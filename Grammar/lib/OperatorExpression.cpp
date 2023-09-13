#include "OperatorExpression.h"

using namespace std;
using namespace Cyclebite::Grammar;

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
    string expr = Graph::OperationToString.at(op) + string(" (");
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
    return expr;
}