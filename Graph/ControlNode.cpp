#include "ControlNode.h"
#include "UnconditionalEdge.h"
#include <algorithm>

using namespace Cyclebite::Graph;

ControlNode::ControlNode() : GraphNode()
{
    blocks = std::set<int64_t>();
}

bool ControlNode::addBlock(int64_t newBlock)
{
    auto i = blocks.insert(newBlock);
    return i.second;
}

void ControlNode::addBlocks(const std::set<int64_t> &newBlocks)
{
    blocks.insert(newBlocks.begin(), newBlocks.end());
}

bool ControlNode::mergeSuccessor(const ControlNode &succ)
{
    // the blocks of the node simply get added, if unique
    blocks.insert(succ.blocks.begin(), succ.blocks.end());
    // the original blocks have to be added in order such that we preserve which original block ID is the current block, and which blocks preceded it (in the order they executed)
    for (const auto &block : succ.originalBlocks)
    {
        bool found = false;
        for (const auto &b : originalBlocks)
        {
            if (block == b)
            {
                found = true;
            }
        }
        if (!found)
        {
            originalBlocks.push_back(block);
        }
    }
    return true;
}

const std::set<std::shared_ptr<UnconditionalEdge>, GECompare> ControlNode::getPredecessors() const
{
    std::set<std::shared_ptr<UnconditionalEdge>, GECompare> converted;
    for( const auto& pred : predecessors )
    {
        if( auto ue = std::dynamic_pointer_cast<UnconditionalEdge>(pred) )
        {
            converted.insert(ue);
        }
    }
    return converted;
}

const std::set<std::shared_ptr<UnconditionalEdge>, GECompare> ControlNode::getSuccessors() const
{
    std::set<std::shared_ptr<UnconditionalEdge>, GECompare> converted;
    for( const auto& succ : successors )
    {
        if( auto ue = std::dynamic_pointer_cast<UnconditionalEdge>(succ) )
        {
            converted.insert(ue);
        }
    }
    return converted;
}

void ControlNode::addPredecessor(std::shared_ptr<UnconditionalEdge> newEdge)
{
    predecessors.insert(newEdge);
}

void ControlNode::removePredecessor(std::shared_ptr<UnconditionalEdge> oldEdge)
{
    predecessors.erase(oldEdge);
}

void ControlNode::addSuccessor(std::shared_ptr<UnconditionalEdge> newEdge)
{
    successors.insert(newEdge);
}

void ControlNode::removeSuccessor(std::shared_ptr<UnconditionalEdge> oldEdge)
{
    successors.erase(oldEdge);
}