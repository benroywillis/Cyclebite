#include "GraphNode.h"

using namespace TraceAtlas::Graph;

uint64_t GraphNode::nextNID = 0;

GraphNode::GraphNode()
{
    NID = getNextNID();
    successors = std::set<std::shared_ptr<GraphEdge>, GECompare>();
    predecessors = std::set<std::shared_ptr<GraphEdge>, GECompare>();
}

GraphNode::~GraphNode() = default;

uint64_t GraphNode::getNextNID()
{
    return nextNID++;
}

std::shared_ptr<GraphEdge> GraphNode::isPredecessor(std::shared_ptr<GraphNode> succ) const
{
    for (const auto &s : successors)
    {
        if (s->getSnk() == succ)
        {
            return s;
        }
    }
    return nullptr;
}

std::shared_ptr<GraphEdge> GraphNode::isSuccessor(std::shared_ptr<GraphNode> pred) const
{
    for (const auto &p : predecessors)
    {
        if (p->getSrc() == pred)
        {
            return p;
        }
    }
    return nullptr;
}

const std::set<std::shared_ptr<GraphEdge>, GECompare>& GraphNode::getPredecessors() const
{
    return predecessors;
}

const std::set<std::shared_ptr<GraphEdge>, GECompare>& GraphNode::getSuccessors() const
{
    return successors;
}

void GraphNode::addPredecessor(std::shared_ptr<GraphEdge> newEdge)
{
    predecessors.insert(newEdge);
}

void GraphNode::removePredecessor(std::shared_ptr<GraphEdge> oldEdge)
{
    predecessors.erase(oldEdge);
}

void GraphNode::addSuccessor(std::shared_ptr<GraphEdge> newEdge)
{
    successors.insert(newEdge);
}

void GraphNode::removeSuccessor(std::shared_ptr<GraphEdge> oldEdge)
{
    successors.erase(oldEdge);
}