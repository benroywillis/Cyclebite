#include "ReductionVariable.h"

using namespace std;
using namespace Cyclebite::Grammar;

ReductionVariable::ReductionVariable( const shared_ptr<InductionVariable>& iv, const shared_ptr<Cyclebite::Graph::DataNode>& n ) : Symbol("rv"), iv(iv), node(n)
{
    // incoming datanode must map to a binary operation
    if( const auto& op = llvm::dyn_cast<llvm::BinaryOperator>(n->getInst()) )
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

const shared_ptr<Cyclebite::Graph::DataNode>& ReductionVariable::getNode() const
{
    return node;
}