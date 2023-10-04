// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
#include "ReductionVariable.h"

using namespace std;
using namespace Cyclebite::Grammar;

ReductionVariable::ReductionVariable( const shared_ptr<InductionVariable>& iv, const shared_ptr<Cyclebite::Graph::DataValue>& n ) : Symbol("rv"), iv(iv), node(n)
{
    // incoming datanode must map to a binary operation
    if( const auto& op = llvm::dyn_cast<llvm::BinaryOperator>(n->getVal()) )
    {
        bin = Cyclebite::Graph::GetOp(op->getOpcode());
    }
}

string ReductionVariable::dump() const 
{
    return name;
}

Cyclebite::Graph::Operation ReductionVariable::getOp() const
{
    return bin;
}

const shared_ptr<Cyclebite::Graph::DataValue>& ReductionVariable::getNode() const
{
    return node;
}