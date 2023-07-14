#include "ControlBlock.h"
#include "DataNode.h"

using namespace Cyclebite::Graph;

ControlBlock::ControlBlock(std::shared_ptr<ControlNode> node, std::set<std::shared_ptr<DataNode>, p_GNCompare> inst) : ControlNode(*node)
{
    instructions.insert(inst.begin(), inst.end());
}