//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "Symbol.h"

using namespace Cyclebite::Grammar;
using namespace std;

Symbol::Symbol(string n) : UID(getNextUID()) 
{
    if( n.empty() )
    {
        name = "Symbol"+to_string(UID);
    }
    else
    {
        name = n+to_string(UID);
    }
}

string Symbol::getName() const
{
    return name;
}

uint64_t Symbol::getID() const
{
    return UID;
}

string Symbol::dump() const
{
    return name;
}

string Symbol::dumpHalide( const map<shared_ptr<Dimension>, shared_ptr<ReductionVariable>>& dimToRV ) const
{
    return name;
}

uint64_t Symbol::getNextUID()
{
    return nextUID++;
}

uint64_t Symbol::nextUID = 0;