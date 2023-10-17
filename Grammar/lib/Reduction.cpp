//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "Reduction.h"

using namespace std;
using namespace Cyclebite::Grammar;

Reduction::Reduction(const shared_ptr<ReductionVariable>& v, const vector<shared_ptr<Symbol>>& in, const vector<Cyclebite::Graph::Operation>& o, const shared_ptr<Symbol>& out ) : Expression(in, o, out, "reduction"), var(v) {}

const shared_ptr<ReductionVariable>& Reduction::getRV() const
{
    return var;
}

string Reduction::dump() const
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
        expr += name + " " + Cyclebite::Graph::OperationToString.at(var->getOp()) + "= ";
    }
    printedName = true;
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
    printedName = flip ? !printedName : printedName;
    return expr;
}