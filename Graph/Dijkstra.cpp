#include "Dijkstra.h"
#include "Graph.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <map>

using namespace std;
using namespace TraceAtlas::Graph;

struct DijkstraCompare
{
    using is_transparent = void;
    bool operator()(const DijkstraNode &lhs, const DijkstraNode &rhs) const
    {
        return lhs.distance < rhs.distance;
    }
} DCompare;

DijkstraNode::DijkstraNode(double d, uint64_t id, uint64_t p, NodeColor c)
{
    distance = d;
    NID = id;
    predecessor = p;
    color = c;
}

set<uint64_t> TraceAtlas::Graph::Dijkstras(const Graph &graph, uint64_t source, uint64_t sink)
{
    // maps a node ID to its dijkstra information
    map<uint64_t, DijkstraNode> DMap;
    for (const auto &node : graph.nodes())
    {
        // initialize each dijkstra node to have infinite distance, itself as its predecessor, and the unvisited nodecolor
        DMap[node->NID] = DijkstraNode(INFINITY, node->NID, std::numeric_limits<uint64_t>::max(), NodeColor::White);
    }
    DMap[source] = DijkstraNode(0, source, std::numeric_limits<uint64_t>::max(), NodeColor::White);
    // priority queue that holds all newly discovered nodes. Minimum paths get priority
    // this deque gets sorted before each iteration, emulating the behavior of a priority queue, which is necessary because std::priority_queue does not support DECREASE_KEY operation
    deque<DijkstraNode> Q;
    Q.push_back(DMap[source]);
    while (!Q.empty())
    {
        // sort the priority queue
        std::sort(Q.begin(), Q.end(), DCompare);
        // for each neighbor of u, calculate the successors new distance
        if (graph.find_node(Q.front().NID))
        {
            for (const auto &neighbor : (graph.getOriginalNode(Q.front().NID))->getSuccessors())
            {
                if (graph.find(neighbor))
                {
                    if (neighbor->getSnk()->NID == source)
                    {
                        // we've found a loop
                        // the DMap distance will be 0 for the source node so we can't do a comparison of distances on the first go-round
                        // if the source doesnt yet have a predecessor then update its stats
                        if (DMap[source].predecessor == std::numeric_limits<uint64_t>::max())
                        {
                            DMap[source].predecessor = Q.front().NID;
                            DMap[source].distance = -log(neighbor->getWeight()) + DMap[Q.front().NID].distance;
                        }
                    }
                    if (-log(neighbor->getWeight()) + Q.front().distance < DMap[neighbor->getSnk()->NID].distance)
                    {
                        DMap[neighbor->getSnk()->NID].predecessor = Q.front().NID;
                        DMap[neighbor->getSnk()->NID].distance = -log(neighbor->getWeight()) + DMap[Q.front().NID].distance;
                        if (DMap[neighbor->getSnk()->NID].color == NodeColor::White)
                        {
                            DMap[neighbor->getSnk()->NID].color = NodeColor::Grey;
                            Q.push_back(DMap[neighbor->getSnk()->NID]);
                        }
                        else if (DMap[neighbor->getSnk()->NID].color == NodeColor::Grey)
                        {
                            // we have already seen this neighbor, it must be in the queue. We have to find its queue entry and update it
                            for (auto &node : Q)
                            {
                                if (node.NID == DMap[neighbor->getSnk()->NID].NID)
                                {
                                    node.predecessor = DMap[neighbor->getSnk()->NID].predecessor;
                                    node.distance = DMap[neighbor->getSnk()->NID].distance;
                                }
                            }
                            std::sort(Q.begin(), Q.end(), DCompare);
                        }
                    }
                }
            }
        }
        DMap[Q.front().NID].color = NodeColor::Black;
        Q.pop_front();
    }
    // now construct the min path
    set<uint64_t> newKernel;
    for (const auto &DN : DMap)
    {
        if (DN.first == sink)
        {
            if (DN.second.predecessor == std::numeric_limits<uint64_t>::max())
            {
                // there was no path found between source and sink
                return newKernel;
            }
            auto prevNode = DN.second.predecessor;
            newKernel.insert(graph.getOriginalNode(prevNode)->NID);
            while (prevNode != source)
            {
                prevNode = DMap[prevNode].predecessor;
                newKernel.insert(graph.getOriginalNode(prevNode)->NID);
            }
            break;
        }
    }
    return newKernel;
}

/// Returns true if one or more cycles exist in the graph specified by nodes, false otherwise
/// The source node passed to this method must be the entrance node of the subgraph
/// This algorithm has no way of looking behind
bool TraceAtlas::Graph::FindCycles(const Graph &graph)
{
    // set of nodes that have been visited at least once (ie they are in the queue)
    set<std::shared_ptr<GraphNode>, p_GNCompare> visited;
    // algorithm inspired by https://www.baeldung.com/cs/detecting-cycles-in-directed-graph
    // queue of nodes that have been touched but their successors have not been fully evaluated yet
    // a node is removed from the queue when all its outgoing edges have been explored
    deque<std::shared_ptr<GraphNode>> Q;
    // this while loop ensures that all nodes in the subgraph are explored
    // it handles the case of "cross-edges" or edges that can go from one dfs tree to another, but the other dfs tree cannot go to the first
    // this makes the algorithm dependent on which node is pushed into the Q first, and thus, the outer while loop ensures that we explore each dfs tree at least once
    while (visited.size() < graph.node_count())
    {
        for (auto node : graph.nodes())
        {
            if (visited.find(node) == visited.end())
            {
                Q.push_front(node);
                break;
            }
        }
        while (!Q.empty())
        {
            visited.insert(Q.front());
            bool pushedNeighbor = false;
            for (const auto &n : Q.front()->getSuccessors())
            {
                if (!graph.find(n))
                {
                    continue;
                }
                if (!graph.find(n->getSnk()))
                {
                    // this node is outside the boundaries of the subgraph, skip
                    continue;
                }
                // since this is a depth first search, then whenever we have a neighbor that is in the Q, we have found a "back edge" in the depth of the tree ie a cycle
                for (const auto &entry : Q)
                {
                    if (entry == n->getSnk())
                    {
                        return true;
                    }
                }
                if (visited.find(n->getSnk()) == visited.end())
                {
                    Q.push_front(n->getSnk());
                    pushedNeighbor = true;
                    // process successors one at a time, we will eventually circle back to this node in the queue
                    // this enforces the depth first search
                    break;
                }
            }
            if (!pushedNeighbor)
            {
                Q.pop_front();
            }
        }
    }
    return false;
}

void Unblock(std::shared_ptr<GraphNode> node, set<std::shared_ptr<GraphNode>, p_GNCompare> &blocked, map<std::shared_ptr<GraphNode>, set<std::shared_ptr<GraphNode>, p_GNCompare>, p_GNCompare> &B)
{
    blocked.erase(node);
    if (B.find(node) == B.end())
    {
        B.insert(pair<std::shared_ptr<GraphNode>, set<std::shared_ptr<GraphNode>, p_GNCompare>>(node, set<std::shared_ptr<GraphNode>, p_GNCompare>()));
    }
    for (const auto &n : B.at(node))
    {
        //B.at(node).erase(n);
        if (blocked.find(n) != blocked.end())
        {
            Unblock(n, blocked, B);
        }
    }
    B.at(node).clear();
}

bool Circuit(const std::set<std::shared_ptr<GraphNode>, p_GNCompare> &subgraph, std::shared_ptr<GraphNode> v, std::shared_ptr<GraphNode> source, set<std::shared_ptr<GraphNode>, p_GNCompare> &blocked, vector<set<std::shared_ptr<GraphNode>>> &cycles, deque<std::shared_ptr<GraphNode>> &currentPath, map<std::shared_ptr<GraphNode>, set<std::shared_ptr<GraphNode>, p_GNCompare>, p_GNCompare> &B)
{
    bool foundCircuit = false;
    Unblock(v, blocked, B);
    currentPath.push_back(v);
    blocked.insert(v);
    for (auto nei : v->getSuccessors())
    {
        auto succ = subgraph.find(nei->getSnk()->NID);
        if (succ != subgraph.end())
        {
            if (*succ == source)
            {
                foundCircuit = true;
                currentPath.push_back(*succ);
                cycles.push_back(set<std::shared_ptr<GraphNode>>(currentPath.begin(), currentPath.end()));
                currentPath.pop_back();
            }
            else if (blocked.find(*succ) == blocked.end())
            {
                foundCircuit = Circuit(subgraph, *succ, source, blocked, cycles, currentPath, B);
            }
        }
    }
    if (foundCircuit)
    {
        Unblock(v, blocked, B);
    }
    else
    {
        for (auto &nei : v->getSuccessors())
        {
            auto succ = subgraph.find(nei->getSnk()->NID);
            if (succ != subgraph.end())
            {
                B.at(v).insert(*succ);
            }
        }
    }
    currentPath.pop_back();
    return foundCircuit;
}

vector<set<std::shared_ptr<GraphNode>>> TraceAtlas::Graph::FindAllUniqueCycles(const std::set<std::shared_ptr<GraphNode>, p_GNCompare> &subgraph)
{
    // based on an algorithm from https://www.cs.tufts.edu/comp/150GA/homeworks/hw1/Johnson%2075.PDF
    // array of sets of nodes that describe each unique cycle found
    vector<set<std::shared_ptr<GraphNode>>> cycles;
    if (subgraph.empty())
    {
        return cycles;
    }
    // array of nodes that are currently blocked from being evaluated by the algorithm
    set<std::shared_ptr<GraphNode>, p_GNCompare> blocked;
    // maps a node to its successors that have already been investigated
    map<std::shared_ptr<GraphNode>, set<std::shared_ptr<GraphNode>, p_GNCompare>, p_GNCompare> B;
    // nodes that lie along the current path (which may lead to a circuit)
    deque<std::shared_ptr<GraphNode>> currentPath;
    // source node, the origin of the current path
    auto it_s = subgraph.begin();
    while (it_s != subgraph.end())
    {
        auto s = *it_s;
        for (auto &nei : s->getSuccessors())
        {
            auto succ = subgraph.find(nei->getSnk()->NID);
            if (succ != subgraph.end())
            {
                blocked.erase(*succ);
                if (B.find(*succ) == B.end())
                {
                    B.insert(pair<std::shared_ptr<GraphNode>, set<std::shared_ptr<GraphNode>, p_GNCompare>>(*succ, set<std::shared_ptr<GraphNode>, p_GNCompare>()));
                }
                B.at(*succ).clear();
            }
        }
        Circuit(subgraph, s, s, blocked, cycles, currentPath, B);
        it_s++;
    }
    return cycles;
}