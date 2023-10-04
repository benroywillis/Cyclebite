// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
#include "Reduction.h"

using namespace std;
using namespace Cyclebite::Grammar;

Reduction::Reduction(const shared_ptr<ReductionVariable>& v, const vector<shared_ptr<Symbol>>& s, const vector<Cyclebite::Graph::Operation>& o ) : Expression(s, o), var(v) {}

string Reduction::dump() const
{
    string expr = name + " " + Cyclebite::Graph::OperationToString.at(var->getOp()) + "= ";
    if( !symbols.empty() )
    {
        auto b = symbols.begin();
        auto o = ops.begin();
        expr += " "+(*b)->dump();
        b = next(b);
        while( b != symbols.end() )
        {
            expr += " "+string(Cyclebite::Graph::OperationToString.at(*o))+" "+(*b)->dump();
            b = next(b);
            o = next(o);
        }
    }
    return expr;
}