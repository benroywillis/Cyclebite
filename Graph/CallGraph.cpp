// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
#include "CallGraph.h"
#include "Util/Exceptions.h"

using namespace Cyclebite::Graph;
using namespace std;

CallGraph::CallGraph() : Graph() {}

CallGraph::CallGraph(const std::set<std::shared_ptr<CallGraphNode>, CGNCompare> &nodeSet, const std::set<std::shared_ptr<CallGraphEdge>, GECompare> &edgeSet) : Graph(NodeConvert(nodeSet), EdgeConvert(edgeSet))
{
    for (const auto &n : nodeSet)
    {
        CGN.insert(n);
    }
}

const set<shared_ptr<CallGraphNode>, CGNCompare> CallGraph::getCallNodes() const
{
    std::set<std::shared_ptr<CallGraphNode>, CGNCompare> converted;
    std::transform(nodeSet.begin(), nodeSet.end(), std::inserter(converted, converted.begin()), [](const std::shared_ptr<GraphNode> &down) { return std::static_pointer_cast<CallGraphNode>(down); });
    return converted;
}

bool CallGraph::find(const llvm::Function *f) const
{
    return CGN.find(f) != CGN.end();
}

const std::shared_ptr<CallGraphNode> &CallGraph::operator[](const llvm::Function *f) const
{
    auto it = CGN.find(f);
    if (it == CGN.end())
    {
        throw CyclebiteException("Function " + string(f->getName()) + " not found in callgraph!");
    }
    return *it;
}

void CallGraph::addNode(const std::shared_ptr<CallGraphNode> &a)
{
    nodeSet.insert(a);
    CGN.insert(a);
}

void CallGraph::addNodes(const std::set<std::shared_ptr<CallGraphNode>, CGNCompare> &nodes)
{
    nodeSet.insert(nodes.begin(), nodes.end());
}

const shared_ptr<CallGraphNode> CallGraph::getMainNode() const
{
    shared_ptr<CallGraphNode> main;
    for( const auto& node : nodeSet )
    {
        if( node->getPredecessors().empty() )
        {
            if( main != nullptr ) 
            {
                throw CyclebiteException("Found more than one main node!");
            }
            main = static_pointer_cast<CallGraphNode>(node);
        }
    }
    if( main == nullptr )
    {
        throw CyclebiteException("Callgraph does not have a main node!");
    }
    return static_pointer_cast<CallGraphNode>(*nodeSet.find(main));
}