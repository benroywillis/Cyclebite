#include "CallNode.h"

using namespace std;
using namespace Cyclebite::Graph;

CallNode::CallNode( const llvm::Instruction* inst, const set<shared_ptr<ControlBlock>, p_GNCompare>& dests ) : Inst(inst)
{
    for( const auto& d : dests )
    {
        destinations.insert(d);
    }
}

CallNode::CallNode(const Inst *upgrade, const set<shared_ptr<ControlBlock>, p_GNCompare>& dests ) : Inst(*upgrade)
{
    for( const auto& d : dests )
    {
        destinations.insert(d);
    }
}

const set<shared_ptr<ControlBlock>, p_GNCompare>& CallNode::getDestinations() const
{
    return destinations;
}