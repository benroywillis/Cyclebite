#pragma once
#include <map>
#include <set>
#include <vector>

namespace TraceAtlas::Graph
{
    class ControlNode;
    class VKNode;
    struct p_GNCompare;
} // namespace TraceAtlas::Graph

namespace TraceAtlas::Cartographer
{
    void AddNode(std::set<TraceAtlas::Graph::ControlNode *, TraceAtlas::Graph::p_GNCompare> &nodes, const TraceAtlas::Graph::ControlNode &newNode);
    void AddNode(std::set<TraceAtlas::Graph::ControlNode *, TraceAtlas::Graph::p_GNCompare> &nodes, const TraceAtlas::Graph::VKNode &newNode);
    void RemoveNode(std::set<TraceAtlas::Graph::ControlNode *, TraceAtlas::Graph::p_GNCompare> &CFG, TraceAtlas::Graph::ControlNode *removeNode);
    void RemoveNode(std::set<TraceAtlas::Graph::ControlNode *, TraceAtlas::Graph::p_GNCompare> &CFG, const TraceAtlas::Graph::ControlNode &removeNode);
} // namespace TraceAtlas::Cartographer