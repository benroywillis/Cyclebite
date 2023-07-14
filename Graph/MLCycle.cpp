#include "MLCycle.h"
#include "Dijkstra.h"
#include "UnconditionalEdge.h"
#include <algorithm>
#include <memory>

using namespace std;
using namespace Cyclebite::Graph;

uint32_t MLCycle::nextKID = 0;

MLCycle::MLCycle()
{
    KID = getNextKID();
    childKernels = std::set<std::shared_ptr<MLCycle>, p_GNCompare>();
    parentKernels = std::set<std::shared_ptr<MLCycle>, p_GNCompare>();
    Label = "";
}

bool MLCycle::addNode(const std::shared_ptr<ControlNode> &newNode)
{
    auto i = subgraph.insert(newNode);
    blocks.insert(newNode->blocks.begin(), newNode->blocks.end());
    if (auto ML = dynamic_pointer_cast<MLCycle>(newNode))
    {
        childKernels.insert(ML);
    }
    deque<shared_ptr<ControlNode>> Q;
    Q.push_back(newNode);
    while (!Q.empty())
    {
        if (const auto &ML = dynamic_pointer_cast<MLCycle>(Q.front()))
        {
            childKernels.insert(ML);
        }
        else if (const auto &VN = dynamic_pointer_cast<VirtualNode>(Q.front()))
        {
            for (const auto &sub : VN->getSubgraph())
            {
                Q.push_back(sub);
            }
        }
        Q.pop_front();
    }
    return i.second;
}

void MLCycle::addNodes(const std::set<std::shared_ptr<ControlNode>, p_GNCompare> &newNodes)
{
    subgraph.insert(newNodes.begin(), newNodes.end());
    // here we recurse through the virtual nodes to try and find child kernels
    // MLCycles can be buried beneath many layers of virtual nodes (because we combine cycle finding with transforms and the shared function transform)
    // we recurse through all layers of virtual nodes looking for MLCycles
    // we do not recurse into MLCycles because that would cross hierarchical boundaries
    deque<shared_ptr<ControlNode>> Q;
    for (const auto &node : newNodes)
    {
        Q.push_back(node);
    }
    while (!Q.empty())
    {
        if (const auto &ML = dynamic_pointer_cast<MLCycle>(Q.front()))
        {
            childKernels.insert(ML);
            ML->addParentKernel(make_shared<MLCycle>(*this));
        }
        else if (const auto &VN = dynamic_pointer_cast<VirtualNode>(Q.front()))
        {
            for (const auto &sub : VN->getSubgraph())
            {
                Q.push_back(sub);
            }
        }
        Q.pop_front();
    }
    // add blocks that are exclusive to this kernel (ie take out all blocks that belong to children)
    blocks.clear();
    for (const auto &node : subgraph)
    {
        blocks.insert(node->blocks.begin(), node->blocks.end());
    }
    set<int64_t> setDiff;
    for (auto &child : childKernels)
    {
        set_difference(blocks.begin(), blocks.end(), child->blocks.begin(), child->blocks.end(), std::inserter(setDiff, setDiff.begin()));
        blocks = setDiff;
    }
}

/// @brief Compares this kernel to another kernel by measuring node differences, then returns the nodes that are shared between the two kernels (this kernel and the argument)
std::set<std::shared_ptr<ControlNode>, p_GNCompare> MLCycle::Compare(const MLCycle &compare) const
{
    std::set<std::shared_ptr<ControlNode>, p_GNCompare> shared;
    for (const auto &compNode : compare.subgraph)
    {
        if (subgraph.find(compNode) != subgraph.end())
        {
            shared.insert(compNode);
        }
    }
    return shared;
}

/// Returns true if any node in the kernel can reach every other node in the kernel. False otherwise
bool MLCycle::FullyConnected() const
{
    for (const auto &node : subgraph)
    {
        // keeps track of which nodeIDs have been visited, all initialized to white
        std::map<uint64_t, NodeColor> colors;
        for (const auto &node2 : subgraph)
        {
            colors[node2->NID] = NodeColor::White;
        }
        // holds newly discovered nodes
        std::deque<std::shared_ptr<ControlNode>> Q;
        Q.push_back(node);
        while (!Q.empty())
        {
            for (const auto &neighbor : Q.front()->getSuccessors())
            {
                // check if this neighbor is within the kernel
                if (subgraph.find(neighbor->getSnk()->NID) != subgraph.end())
                {
                    if (colors[neighbor->getSnk()->NID] == NodeColor::White)
                    {
                        Q.push_back(*(subgraph.find(neighbor->getSnk()->NID)));
                    }
                }
                colors[neighbor->getSnk()->NID] = NodeColor::Black;
            }
            Q.pop_front();
        }
        // if any nodes in the kernel have not been touched, this node couldn't reach them
        for (const auto &node : colors)
        {
            if (node.second == NodeColor::White)
            {
                return false;
            }
        }
    }
    return true;
}

float MLCycle::PathProbability() const
{
    // our convention penalizes kernels with more than one exit
    // the probabilities of edges that leave the kernel are summed
    float pathProbability = 1.0f;
    for (const auto &e : edges)
    {
        pathProbability *= (float)e->getWeight();
    }
    return pathProbability;
}

int MLCycle::EnExScore() const
{
    return (int)getEntrances().size() + (int)getExits().size();
}

inline bool MLCycle::operator==(const MLCycle &rhs) const
{
    return rhs.KID == KID;
}

const set<shared_ptr<MLCycle>, p_GNCompare> &MLCycle::getChildKernels() const
{
    return childKernels;
}

const set<shared_ptr<MLCycle>, p_GNCompare> &MLCycle::getParentKernels() const
{
    return parentKernels;
}

uint32_t MLCycle::getNextKID()
{
    return nextKID++;
}

void MLCycle::addParentKernel(shared_ptr<MLCycle> parent)
{
    parentKernels.insert(parent);
}

void MLCycle::removeParentKernel(const shared_ptr<MLCycle>& parent)
{
    parentKernels.erase(parent);
}