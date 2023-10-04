//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "VirtualNode.h"
#include "Util/Exceptions.h"
#include "UnconditionalEdge.h"

using namespace Cyclebite::Graph;
using namespace std;

VirtualNode::VirtualNode() : ControlNode()
{
    subgraph = set<std::shared_ptr<ControlNode>, p_GNCompare>();
    anchor = 0;
}

bool VirtualNode::addNode(const std::shared_ptr<ControlNode> &newNode)
{
    auto i = subgraph.insert(newNode);
    blocks.insert(newNode->blocks.begin(), newNode->blocks.end());
    return i.second;
}

bool VirtualNode::addEdge(const std::shared_ptr<UnconditionalEdge>& newEdge)
{
    auto it = edges.insert(newEdge);
    return it.second;
}

void VirtualNode::addNodes(const set<std::shared_ptr<ControlNode>, p_GNCompare> &newNodes)
{
    subgraph.insert(newNodes.begin(), newNodes.end());
    for (const auto &node : newNodes)
    {
        blocks.insert(node->blocks.begin(), node->blocks.end());
    }
}

void VirtualNode::addEdges(const set<shared_ptr<UnconditionalEdge>, GECompare>& newEdges )
{
    edges.insert(newEdges.begin(), newEdges.end());
}

const std::set<std::shared_ptr<ControlNode>, p_GNCompare> &VirtualNode::getSubgraph() const
{
    return subgraph;
}

bool VirtualNode::find(const shared_ptr<ControlNode> &search) const
{
    return subgraph.find(search) != subgraph.end();
}

const std::set<std::shared_ptr<UnconditionalEdge>, GECompare> &VirtualNode::getSubgraphEdges() const
{
    return edges;
}

/// Returns the IDs of the kernel entrances
/// @retval    entrances Vector of IDs that specify which nodes (or blocks) are the kernel entrances. Kernel entrances are the source nodes of edges that enter the kernel
std::vector<std::shared_ptr<UnconditionalEdge>> VirtualNode::getEntrances() const
{
    std::vector<std::shared_ptr<UnconditionalEdge>> entrances;
    for (const auto &node : subgraph)
    {
        for (const auto &pred : node->getPredecessors())
        {
            if( edges.find(pred) == edges.end() )
            {
                // this node has a predecessor outside of the kernel, so it is considered an entrance node
                entrances.push_back(pred);
            }
        }
    }
    return entrances;
}

/// @brief Returns the IDs of the blocks from the original bitcode that are entrances of the kernel (they have a predecessor that is outside the kernel)
std::set<uint32_t> VirtualNode::getEntranceBlocks(uint32_t markovOrder) const
{
    std::set<uint32_t> entBlockIDs;
    if (markovOrder == 0)
    {
        return entBlockIDs;
    }
    for (const auto &ent : getEntrances())
    {
        if (ent->getWeightedSnk())
        {
            entBlockIDs.insert(ent->getWeightedSnk()->originalBlocks.back());
        }
    }
    return entBlockIDs;
}

/// @brief Returns the IDs of the kernel exits
///
/// @param[in] allNodes Set of all nodes in the control flow graph. Used to copy the nodes that are the destinations of edges that leave the kernel
/// @retval    exits    Vector of IDs that specify which nodes (or blocks) are the kernel exits. Kernel exits are nodes that have a neighbor outside the kernel
std::vector<std::shared_ptr<UnconditionalEdge>> VirtualNode::getExits() const
{
    std::vector<std::shared_ptr<UnconditionalEdge>> exitNodes;
    for (const auto &node : subgraph)
    {
        for (const auto &neighbor : node->getSuccessors())
        {
            if (edges.find(neighbor) == edges.end())
            {
                // we've found an exit
                exitNodes.push_back(neighbor);
            }
        }
    }
    return exitNodes;
}

/// @brief Returns the IDs of the blocks from the original bitcode that are exits of the kernel (they have a successor that is outside the kernel)
std::set<uint32_t> VirtualNode::getExitBlocks(uint32_t markovOrder) const
{
    std::set<uint32_t> exitBlockIDs;
    if (markovOrder == 0)
    {
        return exitBlockIDs;
    }
    for (const auto &exit : getExits())
    {
        if (exit->getWeightedSrc())
        {
            exitBlockIDs.insert(exit->getWeightedSrc()->originalBlocks.back());
        }
    }
    return exitBlockIDs;
}

uint64_t VirtualNode::getAnchor()
{
    for (const auto &node : subgraph)
    {
        uint64_t count = 0;
        /*if (auto child = dynamic_pointer_cast<VirtualNode>(node))
        {
            count += child->getAnchor();
        }*/
        for (const auto &pred : node->getPredecessors())
        {
            count += pred->getFreq();
        }
        if (count > anchor)
        {
            anchor = count;
        }
    }
    return anchor;
}