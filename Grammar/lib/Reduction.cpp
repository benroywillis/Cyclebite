//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "Reduction.h"
#include "Util/Exceptions.h"

using namespace std;
using namespace Cyclebite::Grammar;

Reduction::Reduction(const shared_ptr<Task>& ta, 
                     const shared_ptr<ReductionVariable>& var, 
                     const vector<shared_ptr<Symbol>>& in, 
                     const vector<Cyclebite::Graph::Operation>& o, 
                     const shared_ptr<Symbol>& out ) : Expression(ta, in, o, out, "reduction"), rv(var) {}

const shared_ptr<ReductionVariable>& Reduction::getRV() const
{
    return rv;
}

const shared_ptr<Cycle>& Reduction::getReductionCycles() const
{
    set<shared_ptr<Cycle>> reduxCycles;
    for( const auto& var : vars )
    {
        if( var->getCycle()->find(rv->getNode()) )
        {
            return var->getCycle();
        }
    }
    throw CyclebiteException("Could not find reduction variable cycle!");
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
        expr += name + " " + Cyclebite::Graph::OperationToString.at(rv->getOp()) + "= ";
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