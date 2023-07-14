#include "CallNode.h"

using namespace std;
using namespace TraceAtlas::Graph;

CallNode::CallNode( const llvm::Instruction* inst, const set<shared_ptr<ControlBlock>, p_GNCompare>& dests ) : DataNode(inst)
{
    for( const auto& d : dests )
    {
        destinations.insert(d);
    }
}

CallNode::CallNode(const DataNode *upgrade, const set<shared_ptr<ControlBlock>, p_GNCompare>& dests ) : DataNode(*upgrade)
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