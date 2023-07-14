#pragma once
#include <map>
#include <set>
#include <vector>

namespace Cyclebite::Graph
{
    class ControlNode;
    class VKNode;
    struct p_GNCompare;
} // namespace Cyclebite::Graph

namespace Cyclebite::Cartographer
{
    void AddNode(std::set<Cyclebite::Graph::ControlNode *, Cyclebite::Graph::p_GNCompare> &nodes, const Cyclebite::Graph::ControlNode &newNode);
    void AddNode(std::set<Cyclebite::Graph::ControlNode *, Cyclebite::Graph::p_GNCompare> &nodes, const Cyclebite::Graph::VKNode &newNode);
    void RemoveNode(std::set<Cyclebite::Graph::ControlNode *, Cyclebite::Graph::p_GNCompare> &CFG, Cyclebite::Graph::ControlNode *removeNode);
    void RemoveNode(std::set<Cyclebite::Graph::ControlNode *, Cyclebite::Graph::p_GNCompare> &CFG, const Cyclebite::Graph::ControlNode &removeNode);
} // namespace Cyclebite::Cartographer