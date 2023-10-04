// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
#include "ControlGraph.h"
#include "Util/Exceptions.h"
#include "ImaginaryNode.h"
#include "UnconditionalEdge.h"
#include "ImaginaryEdge.h"

using namespace Cyclebite::Graph;
using namespace std;

ControlGraph::ControlGraph() : Graph() {}

ControlGraph::ControlGraph(const std::set<std::shared_ptr<ControlNode>, p_GNCompare> &nodeSet, const std::set<std::shared_ptr<UnconditionalEdge>, GECompare> &edgeSet, const shared_ptr<ControlNode>& terminator ) : Graph(NodeConvert(nodeSet), EdgeConvert(edgeSet)), programTerminator(terminator) {}

ControlGraph::ControlGraph(const Graph& graph, const shared_ptr<ControlNode>& terminator) : Graph(), programTerminator(terminator)
{
    for( const auto& node : graph.nodes() )
    {
        if( auto cn = dynamic_pointer_cast<ControlNode>(node) )
        {
            nodeSet.insert(cn);
            for( const auto& pred : cn->getPredecessors() )
            {
                edgeSet.insert(static_pointer_cast<GraphEdge>(pred));
            }
            for( const auto& succ : cn->getSuccessors() )
            {
                edgeSet.insert(static_pointer_cast<GraphEdge>(succ));
            }
        }
    }
}

const shared_ptr<ControlNode> ControlGraph::getNode(const shared_ptr<ControlNode> &s) const
{
    return static_pointer_cast<ControlNode>(*nodeSet.find(s));
}

const shared_ptr<ControlNode> ControlGraph::getNode(uint64_t ID) const
{
    return static_pointer_cast<ControlNode>(*nodeSet.find(ID));
}

const shared_ptr<ControlNode> ControlGraph::getFirstNode() const
{
    shared_ptr<ControlNode> firstNode = nullptr;
    for (const auto &node : nodeSet)
    {
        if (node->getPredecessors().size() == 1 )
        {
            if( auto i = dynamic_pointer_cast<ImaginaryEdge>( *(node->getPredecessors().begin())) )
            {
                if( i->isEntrance() )
                {
                    if (firstNode == nullptr)
                    {
                        firstNode = static_pointer_cast<ControlNode>(node);
                    }
                    else
                    {
                        throw AtlasException("Graph has more than one starting node!");
                    }
                }
            }
        }
    }
    if( !firstNode )
    {
        throw AtlasException("Graph does not have a starting node!");
    }
    return firstNode;
}

const shared_ptr<ControlNode> ControlGraph::getProgramTerminator() const
{
    return programTerminator;
}

const set<shared_ptr<ControlNode>, p_GNCompare> ControlGraph::getAllTerminators() const
{
    set<shared_ptr<ControlNode>, p_GNCompare> terminators;
    shared_ptr<ImaginaryNode> graphTerminator = nullptr;
    for( const auto& succ : static_pointer_cast<GraphNode>(programTerminator)->getSuccessors() )
    {
        if( auto i = dynamic_pointer_cast<ImaginaryNode>(succ->getSnk()) )
        {
            graphTerminator = i;
        }
    }
    if( !graphTerminator )
    {
        throw AtlasException("Cannot find the imaginary terminator of this control graph!");
    }
    for( const auto& pred : graphTerminator->getPredecessors() )
    {
        if( auto cn = dynamic_pointer_cast<ControlNode>(pred->getSrc()) )
        {
            terminators.insert(cn);
        }
    }
    return terminators;
}

const set<shared_ptr<ControlNode>, p_GNCompare> ControlGraph::getControlNodes() const
{
    std::set<std::shared_ptr<ControlNode>, p_GNCompare> converted;
    for( const auto& node : nodeSet )
    {
        if( std::dynamic_pointer_cast<ControlNode>(node) == nullptr ) 
        {
            if( auto i = std::dynamic_pointer_cast<ImaginaryNode>(node) )
            {
                // beginning or ending of the program, just skip it bc we don't need to worry about this node
            }
            else
            {
                throw AtlasException("Node cannot be converted to a control node!");
            }
        }
        else
        {
            converted.insert(std::static_pointer_cast<ControlNode>(node)); 
        }
    }
    return converted;
}

const set<shared_ptr<UnconditionalEdge>, GECompare> ControlGraph::getControlEdges() const
{
    std::set<std::shared_ptr<UnconditionalEdge>, GECompare> converted;
    for( const auto& edge : edgeSet )
    {
        if( std::dynamic_pointer_cast<UnconditionalEdge>(edge) == nullptr ) 
        {
            if( auto i = std::dynamic_pointer_cast<ImaginaryNode>(edge) )
            {
                // beginning or ending of the program, just skip it bc we don't need to worry about this edge
            }
            else
            {
                throw AtlasException("Edge cannot be converted to an unconditional node!");
            }
        }
        else
        {
            converted.insert(std::static_pointer_cast<UnconditionalEdge>(edge)); 
        }
    }
    return converted;
}