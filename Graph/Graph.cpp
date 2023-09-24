#include "Graph.h"
#include "Util/Exceptions.h"
#include "ImaginaryEdge.h"

using namespace Cyclebite::Graph;
using namespace std;

Graph::Graph() = default;

Graph::Graph(const std::set<std::shared_ptr<GraphNode>, p_GNCompare> &nodeSet, const std::set<std::shared_ptr<GraphEdge>, GECompare> &edgeSet) : nodeSet(nodeSet), edgeSet(edgeSet) {}

Graph::~Graph() = default;

const shared_ptr<GraphNode> &Graph::getOriginalNode(const shared_ptr<GraphNode> &s) const
{
    return *nodeSet.find(s);
}

const shared_ptr<GraphNode> &Graph::getOriginalNode(uint64_t ID) const
{
    return *nodeSet.find(ID);
}

const set<shared_ptr<GraphNode>, p_GNCompare> &Graph::getNodes() const
{
    return nodeSet;
}

const shared_ptr<GraphEdge> &Graph::getOriginalEdge(const shared_ptr<GraphEdge> &e) const
{
    return *edgeSet.find(e);
}

const set<shared_ptr<GraphEdge>, GECompare> &Graph::getEdges() const
{
    return edgeSet;
}

const set<shared_ptr<GraphNode>, p_GNCompare> Graph::getFirstNodes() const
{
    set<std::shared_ptr<GraphNode>, p_GNCompare> firstNodes;
    for (const auto &node : nodeSet)
    {
        if (node->getPredecessors().empty())
        {
            firstNodes.insert(node);
        }
    }
    return firstNodes;
}

const set<shared_ptr<GraphNode>, p_GNCompare> Graph::getLastNodes() const
{
    set<shared_ptr<GraphNode>, p_GNCompare> lastNodes;
    for (const auto &node : nodeSet)
    {
        if (node->getSuccessors().empty())
        {
            lastNodes.insert(node);
        }
    }
    return lastNodes;
}

void Graph::addNode(const std::shared_ptr<GraphNode> &a)
{
    nodeSet.insert(a);
}

void Graph::addNodes(const std::set<std::shared_ptr<GraphNode>, p_GNCompare> &nodes)
{
    nodeSet.insert(nodes.begin(), nodes.end());
}

void Graph::removeNode(const std::shared_ptr<GraphNode> &r)
{
    nodeSet.erase(r);
}

void Graph::addEdge(const std::shared_ptr<GraphEdge> &a)
{
    edgeSet.insert(a);
}

void Graph::addEdges(const std::set<std::shared_ptr<GraphEdge>, GECompare> &edges)
{
    edgeSet.insert(edges.begin(), edges.end());
}

void Graph::removeEdge(const std::shared_ptr<GraphEdge> &r)
{
    edgeSet.erase(r);
}

bool Graph::find(const std::shared_ptr<GraphNode> &s) const
{
    return nodeSet.find(s) != nodeSet.end();
}

bool Graph::find_node(uint64_t ID) const
{
    return nodeSet.find(ID) != nodeSet.end();
}

bool Graph::find(const std::shared_ptr<GraphEdge> &s) const
{
    return edgeSet.find(s) != edgeSet.end();
}

bool Graph::empty() const
{
    return nodeSet.empty() && edgeSet.empty();
}

void Graph::clear()
{
    nodeSet.clear();
    edgeSet.clear();
}

uint64_t Graph::node_count() const
{
    return (uint64_t)nodeSet.size();
}

uint64_t Graph::edge_count() const
{
    return (uint64_t)edgeSet.size();
}

uint64_t Graph::size() const
{
    return (uint64_t)nodeSet.size() + (uint64_t)edgeSet.size();
}

const std::shared_ptr<GraphNode> &Graph::operator[](const std::shared_ptr<GraphNode> &s) const
{
    auto it = nodeSet.find(s);
    if (it == nodeSet.end())
    {
        throw CyclebiteException("Node not found in graph!");
    }
    return *it;
}

const std::shared_ptr<GraphEdge> &Graph::operator[](const std::shared_ptr<GraphEdge> &f) const
{
    auto it = edgeSet.find(f);
    if (it == edgeSet.end())
    {
        throw CyclebiteException("Edge not found in graph!");
    }
    return *it;
}

Graph::Node_Range Graph::nodes()
{
    return Node_Range{nodeSet.begin(), nodeSet.end()};
}

Graph::Edge_Range Graph::edges()
{
    return Edge_Range{edgeSet.begin(), edgeSet.end()};
}

const Graph::Const_Node_Range Graph::nodes() const
{
    return Const_Node_Range{nodeSet.begin(), nodeSet.end()};
}

const Graph::Const_Edge_Range Graph::edges() const
{
    return Const_Edge_Range{edgeSet.begin(), edgeSet.end()};
}