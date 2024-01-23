//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "Transforms.h"
#include "Util/Annotate.h"
#include "Util/IO.h"
#include "CallEdge.h"
#include "ImaginaryNode.h"
#include "ImaginaryEdge.h"
#include "CallGraph.h"
#include "ControlGraph.h"
#include "Dijkstra.h"
#include "IO.h"
#include "VirtualEdge.h"
#include <deque>
#include <llvm/IR/InstrTypes.h>
#include <spdlog/spdlog.h>

using namespace std;
using namespace Cyclebite::Graph;

/// Maximum size for a bottleneck subgraph transform
constexpr uint32_t MAX_BOTTLENECK_SIZE = 200;
/// Minimum nuumber of child kernels that must be present in a loop comprehension kernel in order to ignore the "every embedded kernel must have a child" rule
constexpr uint32_t MIN_CHILD_KERNEL_EXCEPTION = 5;

/// Maps a node from the profile to a set of Virtual Nodes that represent it
map<shared_ptr<ControlNode>, set<shared_ptr<VirtualNode>, p_GNCompare>, p_GNCompare> NodeToVN;
map<shared_ptr<UnconditionalEdge>, set<shared_ptr<VirtualEdge>, GECompare>, GECompare> EdgeToVE;
/// Keeps track of basic blocks that are dead
set<const llvm::BasicBlock *> Cyclebite::Graph::deadCode;
/// This function returns null when the input basic block could not be found in the NIDMap
/// The NID Map is created when reading in the original dynamic profile, and represents all blocks that were observed during that profile
/// So if a basic block cannot be found in it, it means that basic block was not in the dynamic profile ie it is dead code
shared_ptr<GraphNode> Cyclebite::Graph::BlockToNode(const Graph &graph, const llvm::BasicBlock *block, const std::map<vector<uint32_t>, uint64_t> &NIDMap)
{
    vector<uint32_t> bbID;
    bbID.push_back((uint32_t)Cyclebite::Util::GetBlockID(llvm::cast<llvm::BasicBlock>(block)));
    if (NIDMap.find(bbID) != NIDMap.end())
    {
        if (graph.find_node(NIDMap.at(bbID)))
        {
            return graph.getOriginalNode(NIDMap.at(bbID));
        }
        else
        {
            // the original node that represented this block is somewhere in the graph, but it is being covered up by a virtual node
            // to find the virtual node, we simply iterate through the entire graph (breadth search) and for each virtual node we find, we search its subgraph (depth search)
            // once we find the node that maps to this entry in the NIDMap, we return its parent-most virtual node
            for (const auto &node : graph.nodes())
            {
                if (node->ID() == NIDMap.at(bbID))
                {
                    return node;
                }
                else if (auto VN = dynamic_pointer_cast<VirtualNode>(node))
                {
                    set<shared_ptr<ControlNode>, p_GNCompare> covered;
                    deque<shared_ptr<VirtualNode>> Q;
                    Q.push_back(VN);
                    covered.insert(VN);
                    while (!Q.empty())
                    {
                        for (const auto &subnode : Q.front()->getSubgraph())
                        {
                            if (subnode->ID() == NIDMap.at(bbID))
                            {
                                return VN;
                            }
                            else if (auto subVN = dynamic_pointer_cast<VirtualNode>(subnode))
                            {
                                if (covered.find(subVN) == covered.end())
                                {
                                    Q.push_back(subVN);
                                }
                            }
                        }
                        Q.pop_front();
                    }
                }
            }
            throw CyclebiteException("Could not find a node that maps to basic block ID " + to_string(*bbID.begin()));
        }
    }
    else
    {
        if (deadCode.find(block) == deadCode.end())
        {
            spdlog::warn("BB" + to_string(Cyclebite::Util::GetBlockID(block)) + " is dead.");
            deadCode.insert(block);
        }
        return nullptr;
    }
}

/// @brief Maps a node to its corresponding basic block
/// There are two cases where nodes do not map 1:1 with basic blocks
/// If markovOrder = 1, and after transforms have been applied, virtual nodes now have a subgraph of multiple nodes, each with their own basic block and possibly a subgraph for themselves
/// If markovOrder > 1, nodes map to [markovOrder] blocks
/// In order to map a node with subgraphs to 1 basic block, we take the node that has an edge that exits the subgraph
/// If multiple edges are found to exit the subgraph, and exception is thrown
const llvm::BasicBlock *Cyclebite::Graph::NodeToBlock(const std::shared_ptr<ControlNode> &node, const std::map<int64_t, const llvm::BasicBlock *> &IDToBlock)
{
    shared_ptr<ControlNode> targetNode = nullptr;
    if (auto VN = dynamic_pointer_cast<VirtualNode>(node))
    {
        // we want to map a virtual node to its exit nodes
        if (VN->getExits().size() == 1)
        {
            auto exitEdge = VN->getExits().front();
            // the source node of the exit edge is our node to map to a block
            if (auto VN2 = dynamic_pointer_cast<VirtualNode>(exitEdge->getWeightedSrc()))
            {
                // we have to recurse
                throw CyclebiteException("Recursive NodeToBlock method not implemented!");
            }
            else
            {
                targetNode = exitEdge->getWeightedSrc();
            }
        }
        else if (VN->getExits().empty())
        {
            // this case can occur when the subgraph includes the last node in the graph
            // in this case we map the virtual node to its entrance
            if (VN->getEntrances().empty())
            {
                auto subVN = static_pointer_cast<VirtualNode>(*(VN->getSubgraph().begin()));
                throw CyclebiteException("Virtual Node has no entrances or exits!");
            }
            else
            {
                targetNode = VN->getEntrances().front()->getWeightedSnk();
            }
        }
        else
        {
            // here we have more than one node to choose from... we resolve this tie by taking the node with the lesser NID
            // MLCycles are allowed to have multiple exits and are derived classes of VirtualNodes so they need to be supported in order for this method to work in general
            targetNode = VN->getExits().front()->getWeightedSrc();
        }
    }
    else
    {
        targetNode = node;
    }
    // this must be a control node, use its originalblocks to get the basic block
    if (targetNode->originalBlocks.size())
    {
        return IDToBlock.at(*targetNode->originalBlocks.begin());
    }
    else
    {
        throw CyclebiteException("ControlNode does not have any original blocks!");
    }
}

void Cyclebite::Graph::SumToOne(const std::set<std::shared_ptr<GraphNode>, p_GNCompare> &nodes)
{
    for (const auto &node : nodes)
    {
        if (auto vk = dynamic_pointer_cast<MLCycle>(node))
        {
            continue;
        }
        else if (node->getSuccessors().empty())
        {
            continue;
        }
        else if( auto i = dynamic_pointer_cast<ImaginaryEdge>(*(node->getSuccessors().begin())) )
        {
            continue;
        }
        double sum = 0.0;
        for (const auto &n : node->getSuccessors())
        {
            sum += n->getWeight();
        }
        if ( sum < 0.999 || sum > 1.001 )
        {
            throw CyclebiteException("Outgoing edges do not sum to one!");
        }
    }
}

/// @brief Implements a series of checks on a transformed graph
/// 1. The graph should have at least one node in it
/// 2. For each node, all predecessors and successors are present in the graph
/// 3. Starting from the first node in the graph, every node is reachable
/// 4. Starting from the last node in the graph, every node is reverse-reachable
/// 5. For a given node, all outgoing edge probabilities sum to one
void Cyclebite::Graph::Checks(const ControlGraph &transformed, string step, bool segmentation)
{
    // 1.
    if (transformed.empty())
    {
        throw CyclebiteException(step + ": Transformed graph is empty!");
    }
    // 2.
    for (const auto &node : transformed.nodes())
    {
        for (const auto &pred : node->getPredecessors())
        {
            if( auto i = dynamic_pointer_cast<ImaginaryEdge>(pred) )
            {
                continue;
            }
            if (!transformed.find(pred))
            {
                throw CyclebiteException(step + ": Predecessor edge missing!");
            }
            if (!transformed.find(pred->getSrc()))
            {
                throw CyclebiteException(step + ": Predecessor source missing!");
            }
            if (!transformed.find(pred->getSnk()))
            {
                throw CyclebiteException(step + ": Predecessor sink missing!");
            }
        }
        for (const auto &succ : node->getSuccessors())
        {
            if( auto i = dynamic_pointer_cast<ImaginaryEdge>(succ) )
            {
                continue;
            }
            if (!transformed.find(succ))
            {
                throw CyclebiteException(step + ": Successor missing!");
            }
            if (!transformed.find(succ->getSrc()))
            {
                throw CyclebiteException(step + ": Successor source missing!");
            }
            if (!transformed.find(succ->getSnk()))
            {
                throw CyclebiteException(step + ": Successor sink missing!");
            }
        }
    }
    // 3.
    set<shared_ptr<GraphNode>, p_GNCompare> covered;
    deque<shared_ptr<GraphNode>> Q;
    Q.push_front(transformed.getFirstNode());
    covered.insert(transformed.getFirstNode());
    while (!Q.empty())
    {
        for (const auto &succ : Q.front()->getSuccessors())
        {
            if (covered.find(succ->getSnk()) == covered.end())
            {
                Q.push_back(succ->getSnk());
                covered.insert(succ->getSnk());
            }
        }
        Q.pop_front();
    }
    for (const auto &node : transformed.nodes())
    {
        if (covered.find(node) == covered.end())
        {
            throw CyclebiteException("Node is unreachable!");
        }
    }
    // 4.
    // for this check we need to get the imaginary node that succeeds the terminator
    // this node will lead back to all nodes in the graph, but the terminator may miss thread terminator blocks
    covered.clear();
    for( const auto& t : transformed.getAllTerminators() )
    {
        Q.push_front(t);
        covered.insert(t);
    }
    while (!Q.empty())
    {
        for (const auto &pred : Q.front()->getPredecessors())
        {
            if (covered.find(pred->getSrc()) == covered.end())
            {
                Q.push_back(pred->getSrc());
                covered.insert(pred->getSrc());
            }
        }
        Q.pop_front();
    }
    for (const auto &node : transformed.nodes())
    {
        if (covered.find(node) == covered.end())
        {
            throw CyclebiteException("Node cannot reach program terminator!");
        }
    }
    // 5.
    if( !segmentation )
    {
        for (const auto &node : transformed.nodes())
        {
            if (node->getSuccessors().empty())
            {
                continue;
            }
            else if( auto i = dynamic_pointer_cast<ImaginaryEdge>(*node->getSuccessors().begin()) )
            {
                continue;
            }
            double sum = 0.0;
            for (const auto &succ : node->getSuccessors())
            {
                sum += succ->getWeight();
            }
            if (sum < 0.9999 || sum > 1.0001)
            {
                throw CyclebiteException(step + ": Outgoing edges do not sum to 1!");
            }
        }
    }
}

// This is a map to memoize the result of SubgraphBFS
// It maps an entrance node + exit nodes to a graph it should map to
std::map<std::set<std::shared_ptr<ControlNode>, p_GNCompare>, Graph> subgraphMemo;
/// @brief This method finds all nodes in a subgraph spanning from entrance to the exits passed in the arguments
///
/// Note: this function assumes that the subgraph to be found exists entirely between a unique entrance node (unique as in it cannot also be an exit) and a set of unique exit nodes
/// This means the function will not find the entirety of a recursive function (because when a function returns to itself, it must go beyond its and back into itself)
/// @param subgraph The subgraph as it currently exists. This may be expanded
/// @param entrance The first node in the subgraph. All predecessors are outside the subgraph and all successors are within
/// @param exits    Edges that represent the boundaries of the function. This should include exits of the target function BFS as well as all exits of embedded functions.
/// @retval         Graph with the nodes and edges belonging to the subgraph. Edges that have src nodes or sink nodes outside the subgraph are not included.
ControlGraph SubgraphBFS(const std::shared_ptr<ControlNode> &entrance, const std::set<std::shared_ptr<UnconditionalEdge>, GECompare> &exits)
{
    set<shared_ptr<ControlNode>, p_GNCompare> subNodes;
    /* we don't do memoized results anymore because the virtualization of embedded functions will ruin this result
    // first look for a memoized result
    set<shared_ptr<ControlNode>, p_GNCompare> graphKey;
    graphKey.insert(entrance);
    for( const auto& e : exits )
    {
        graphKey.insert(e->getWeightedSrc());
    }
    if( subgraphMemo.find(graphKey) != subgraphMemo.end() )
    {
        //return subgraphMemo.at(graphKey);
    }*/
    set<shared_ptr<GraphEdge>, GECompare> covered;
    set<shared_ptr<GraphEdge>, GECompare> exitCover;
    subNodes.insert(entrance);
    for (const auto &e : exits)
    {
        subNodes.insert(e->getWeightedSrc());
    }
    deque<shared_ptr<ControlNode>> Q;
    Q.push_front(entrance);
    uint32_t searchIterations = 0;
    while ((exitCover.size() != exits.size()) || !Q.empty())
    {
        if (Q.empty())
        {
            for (const auto &node : subNodes)
            {
                for (const auto &succ : node->getSuccessors())
                {
                    if (covered.find(succ) == covered.end())
                    {
                        if (std::find(Q.begin(), Q.end(), node) == Q.end())
                        {
                            Q.push_front(node);
                        }
                    }
                }
                for (const auto &pred : node->getPredecessors())
                {
                    if (covered.find(pred) == covered.end())
                    {
                        if (std::find(Q.begin(), Q.end(), node) == Q.end())
                        {
                            Q.push_front(pred->getWeightedSrc());
                        }
                    }
                }
            }
        }
        while (!Q.empty())
        {
            for (const auto &succ : Q.front()->getSuccessors())
            {
                if ((covered.find(succ) == covered.end()))
                {
                    if (exits.find(succ) == exits.end())
                    {
                        covered.insert(succ);
                        subNodes.insert(succ->getWeightedSnk());
                        Q.push_back(succ->getWeightedSnk());
                    }
                    else
                    {
                        covered.insert(succ);
                        exitCover.insert(succ);
                    }
                }
            }
            Q.pop_front();
        }
        if (++searchIterations > 1000000)
        {
            throw CyclebiteException("Function subgraph BFS exceeded 1,000,000 iterations!");
        }
    }
    set<shared_ptr<UnconditionalEdge>, GECompare> subEdges;
    for (const auto &n : subNodes)
    {
        for (const auto &pred : n->getPredecessors())
        {
            if (subNodes.find(pred->getWeightedSrc()) != subNodes.end())
            {
                subEdges.insert(pred);
            }
        }
        for (const auto &succ : n->getSuccessors())
        {
            if (subNodes.find(succ->getWeightedSnk()) != subNodes.end())
            {
                subEdges.insert(succ);
            }
        }
    }
    //subgraphMemo[graphKey] = Graph(subNodes, subEdges);
    return ControlGraph(subNodes, subEdges, entrance);
}

/// function exit predication
/// when the function graph has certain exits removed, this can create a part of the function graph that cannot reach the last node in the graph it is being inlined to
/// likewise, it can also create nodes that are not reachable from the first node in the graph
/// we get rid of those nodes with this method
/// @param funcGraph    ControlGraph of the function whose graph we are trying to build. The nodes and edges in the graph are only the nodes and edges that belong to the function subgraph; any node/edge that is/leads outside the function is not included.
/// @param entrance     The calledge to be inlined
/// @param exits        The edges that define the boundaries of the function. This will include the exit(s) of the entrance as well as any other function edges that lead to this function.
void removeUnreachableNodes(ControlGraph &funcGraph, const shared_ptr<CallEdge> &entrance)
{
#ifdef DEBUG
    ofstream GraphBefore("GraphBeforeRemovingUnreachable.dot");
    auto LastGraph = GenerateFunctionSubgraph(funcGraph, entrance);
    GraphBefore << LastGraph << "\n";
    GraphBefore.close();
#endif
    // corner case: when a program exits main from a block that is in this function ; in this case there will be a node with no successors
    // we don't know which function instance to give this exit to... so we through an exception if we find it
    set<shared_ptr<GraphNode>, p_GNCompare> toRemove;
    for (const auto &node : funcGraph.nodes())
    {
        if (node->getSuccessors().empty())
        {
            if( entrance->rets.f )
            {
                throw CyclebiteException("Shared function "+string(entrance->rets.f->getName())+"'s subgraph exits the program!");
            }
            else
            {
                throw CyclebiteException("Found a null shared function subgraph that exits the program!");
            }
        }
    }
    // we have two tests
    // the first walks forward from the entrance; any nodes not touched by this walk are unreachable
    set<shared_ptr<GraphNode>, p_GNCompare> covered;
    deque<shared_ptr<GraphNode>> Q;
    Q.push_front(entrance->getSnk());
    covered.insert(entrance->getSnk());
    while (!Q.empty())
    {
        for (const auto &succ : Q.front()->getSuccessors())
        {
            if (funcGraph.find(succ))
            {
                if (covered.find(succ->getSnk()) == covered.end())
                {
                    Q.push_back(succ->getSnk());
                    covered.insert(succ->getSnk());
                }
            }
        }
        Q.pop_front();
    }
    for (const auto &node : funcGraph.nodes())
    {
        if (covered.find(node) == covered.end())
        {
            toRemove.insert(node);
        }
    }
    // the second walks backward from each exit ; any node not touched by this walk is a dead end
    covered.clear();
    for (const auto &ex : entrance->rets.dynamicRets)
    {
        Q.push_front(ex->getSrc());
        covered.insert(ex->getSrc());
        while (!Q.empty())
        {
            for (const auto &pred : Q.front()->getPredecessors())
            {
                if (funcGraph.find(pred))
                {
                    if (covered.find(pred->getSrc()) == covered.end())
                    {
                        Q.push_back(pred->getSrc());
                        covered.insert(pred->getSrc());
                    }
                }
            }
            Q.pop_front();
        }
    }
    for (const auto &node : funcGraph.nodes())
    {
        if (covered.find(node) == covered.end())
        {
            toRemove.insert(node);
        }
    }
    for (const auto &node : toRemove)
    {
        funcGraph.removeNode(node);
        for (const auto &pred : node->getPredecessors())
        {
            funcGraph.removeEdge(pred);
        }
        for (const auto &succ : node->getSuccessors())
        {
            funcGraph.removeEdge(succ);
        }
    }
#ifdef DEBUG
    ofstream GraphAfter("GraphAfterRemovingUnreachable.dot");
    auto NewGraph = GenerateFunctionSubgraph(funcGraph, entrance);
    GraphAfter << NewGraph << "\n";
    GraphAfter.close();
#endif
}

ControlGraph SimpleFunctionBFS(const std::shared_ptr<CallEdge> &entrance)
{
    // we find all possible function entrances/exits and pass them to the subgraph finder
    // the input calledge only contains one entrance/exit(s) pair, we need them all to distinctly find the boundaries of the function
    set<shared_ptr<UnconditionalEdge>, GECompare> funcExits;
    // this set holds all exit edges of embedded functions that we know the parent function can take
    set<shared_ptr<UnconditionalEdge>, GECompare> embFuncExits;
    for (const auto &node : entrance->rets.functionNodes)
    {
        for (const auto &pred : node->getPredecessors())
        {
            if (const auto &call = dynamic_pointer_cast<CallEdge>(pred))
            {
                // we collect all return edges of all function calls we find
                // this should include the exits of the parent function as well as any embedded functions within the rets.functionNodes set
                funcExits.insert(call->rets.dynamicRets.begin(), call->rets.dynamicRets.end());
                if (call != entrance)
                {
                    // we want to exclude the exits of embedded function call that belong to the parent function
                    if (entrance->rets.functionNodes.find(call->getSrc()) != entrance->rets.functionNodes.end())
                    {
                        for (const auto &ret : call->rets.dynamicRets)
                        {
                            if (entrance->rets.functionNodes.find(ret->getSnk()) != entrance->rets.functionNodes.end())
                            {
                                // this call edge belongs to the parent function, so its return edges are not exits
                                embFuncExits.insert(ret);
                            }
                        }
                    }
                }
            }
        }
    }
    // remove all embedded function exits from funcExits set
    for (const auto &correctEx : embFuncExits)
    {
        funcExits.erase(correctEx);
    }
    // remove all edges that no longer belong in the graph
    // the embedded functions that have already been virtualized are inlined into our own function, they shouldn't have entrances from or exits to other functions anymore
    auto exitsCopy = funcExits;
    for (const auto &ex : exitsCopy)
    {
        if (EdgeToVE.find(ex) != EdgeToVE.end())
        {
            funcExits.erase(ex);
        }
    }
    return SubgraphBFS(entrance->getWeightedSnk(), funcExits);
}

ControlGraph DirectRecursionFunctionBFS(const std::shared_ptr<CallEdge> &entrance)
{
    // the call edge passed to us can be any entrance to the recursive function (either an entrance from outside the function, or a recursive entrance)
    // we need to find the calledge that comes from outside the function in order to find the right exits
    // this is because, when the profiler is read, this call edge was the caller from outside the recursive function (it was a "user" of the recursive function)
    // therefore, the dynamic exits it has in its calledge should be the right ones
    set<shared_ptr<UnconditionalEdge>, GECompare> recursionExits;
    for (const auto &node : entrance->rets.functionNodes)
    {
        for (const auto &pred : node->getPredecessors())
        {
            if (const auto &call = dynamic_pointer_cast<CallEdge>(pred))
            {
                // if this call edge was from outside the function, it is an entrance to the recursive function
                // we are only interested in the exits associated with entrance edges so we gather them
                if (entrance->rets.functionNodes.find(call->getSrc()) == entrance->rets.functionNodes.end())
                {
                    recursionExits.insert(call->rets.dynamicRets.begin(), call->rets.dynamicRets.end());
                }
            }
        }
    }
    // remove all edges that have been virtualized
    auto exitCopy = recursionExits;
    for (const auto &ex : recursionExits)
    {
        if (EdgeToVE.find(ex) != EdgeToVE.end())
        {
            recursionExits.erase(ex);
        }
    }
    return SubgraphBFS(entrance->getWeightedSnk(), recursionExits);
}

ControlGraph IndirectRecursionFunctionBFS(const std::shared_ptr<CallEdge> &entrance)
{
    // the exits of an indirect recursion function include the exits of all functions involved
    // and exit from an indirect recursion is any edge that goes from a function inside the indirect recursion to a function outside the indirect recursion
    // first, collect all call edges in the subgraph (this will grab all functions in the indirect recursion)
    // second, collect all their exits
    // third, collect all their nodes
    // fourth, sort exits by whether they exit the functionNodes set in the calledge rets structure (the functionNodes set contains all nodes of the static function plus all nodes from the functions it calls)
    set<shared_ptr<CallEdge>, GECompare> allCalls;
    set<shared_ptr<ControlNode>, p_GNCompare> allNodes;
    set<shared_ptr<UnconditionalEdge>, GECompare> allExits;
    set<shared_ptr<UnconditionalEdge>, GECompare> indirectExits;
    deque<shared_ptr<CallEdge>> Q;
    set<shared_ptr<CallEdge>, GECompare> covered;
    Q.push_front(entrance);
    covered.insert(entrance);
    allCalls.insert(entrance);
    while (!Q.empty())
    {
        for (const auto &node : Q.front()->rets.functionNodes)
        {
            for (const auto &succ : node->getSuccessors())
            {
                if (const auto &c = dynamic_pointer_cast<CallEdge>(succ))
                {
                    if (covered.find(c) == covered.end())
                    {
                        allCalls.insert(c);
                        covered.insert(c);
                        Q.push_back(c);
                    }
                }
            }
            for (const auto &pred : node->getPredecessors())
            {
                if (const auto &c = dynamic_pointer_cast<CallEdge>(pred))
                {
                    if (covered.find(c) == covered.end())
                    {
                        allCalls.insert(c);
                        covered.insert(c);
                        Q.push_back(c);
                    }
                }
            }
        }
        Q.pop_front();
    }

    for (const auto &ce : allCalls)
    {
        allNodes.insert(ce->rets.functionNodes.begin(), ce->rets.functionNodes.end());
        allExits.insert(ce->rets.dynamicRets.begin(), ce->rets.dynamicRets.end());
    }
    for (const auto &ex : allExits)
    {
        if (allNodes.find(ex->getWeightedSnk()) == allNodes.end())
        {
            indirectExits.insert(ex);
        }
    }
    return SubgraphBFS(entrance->getWeightedSnk(), indirectExits);
}

void Cyclebite::Graph::VirtualizeSubgraph(Graph &graph, std::shared_ptr<VirtualNode> &VN, const ControlGraph &subgraph)
{
    if( subgraph.getNodes().empty() || subgraph.getEdges().empty() )
    {
        throw CyclebiteException("Subgraph for virtualization is empty!");
    }
    VN->addNodes(subgraph.getControlNodes());
    VN->addEdges(subgraph.getControlEdges());
    for (const auto &n : subgraph.getControlNodes())
    {
        NodeToVN[n].insert(VN);
    }
    // first we virtualize the edges of our entrance preds and exit succs
    set<std::shared_ptr<ControlNode>, p_GNCompare> entNodes;
    for (auto ent : VN->getEntrances())
    {
        if( auto i = dynamic_pointer_cast<ImaginaryEdge>(ent) )
        {
            // skip, we don't transform imaginary nodes or edges
        }
        else if ( (VN->find(ent->getWeightedSrc())) && (VN->find(ent->getWeightedSnk())) )
        {
            // this is a circling edge, which by convention needs to be in the successors
            // thus we skip it here, and let the exit node handler do it
        }
        else
        {
            entNodes.insert(ent->getWeightedSrc());
        }
    }
    for (auto ent : entNodes)
    {
        // the process of virtualizing each node with at least one entrance edge is three steps
        // 1. accumulate all the frequencies of outgoing edges
        // 2. accumulate all frequencies of VN edges (the edges entering the VN subgraph)
        // 3. virtualize all VN edges, normalize their frequency sum, insert into the relative edge sets

        // first step, accumulate all outgoing edges
        uint64_t totalFreq = 0;
        for (auto succ : ent->getSuccessors())
        {
            totalFreq += succ->getFreq();
        }
        // second, accumulate edges that enter the VN subgraph
        uint64_t VNfreq = 0;
        set<shared_ptr<UnconditionalEdge>, GECompare> VNEdges;
        for (auto succ : ent->getSuccessors())
        {
            if (VN->find(succ->getWeightedSnk()))
            {
                VNfreq += succ->getFreq();
                VNEdges.insert(succ);
            }
        }
        // third, virtualize
        shared_ptr<VirtualEdge> newEdge = nullptr;
        if( VN->getSubgraph().find(ent) != VN->getSubgraph().end() )
        {
            newEdge = make_shared<VirtualEdge>(VNfreq, VN, VN, VNEdges);
        }
        else
        {
            newEdge = make_shared<VirtualEdge>(VNfreq, ent, VN, VNEdges);
        }
        for (const auto &e : VNEdges)
        {
            EdgeToVE[e].insert(newEdge);
        }
        newEdge->setWeight(totalFreq);
        for (auto edge : VNEdges)
        {
            ent->removeSuccessor(edge);
            graph.removeEdge(edge);
        }
        VN->addPredecessor(newEdge);
        ent->addSuccessor(newEdge);
        graph.addEdge(newEdge);
    }
    auto exEdges = VN->getExits();
    for (auto ex : exEdges)
    {
        // it is possible for an entrance to also be an exit (when an edge from within the subgraph cycles back to an edge within the subgraph)
        // in this case we already handled the edge in the entrances, thus we skip it here
        if( (VN->getSubgraph().find(ex->getSnk()) != VN->getSubgraph().end()) )//|| (ex->getSnk() == VN) )
        {
            //continue;
        }
        // the only thing required for exit edges is to virtualize them
        set<shared_ptr<UnconditionalEdge>, GECompare> replaceEdges;
        replaceEdges.insert(ex);
        shared_ptr<VirtualEdge> newEdge = nullptr;
        if( VN->getSubgraph().find(ex->getSnk()) != VN->getSubgraph().end() )
        {
            newEdge = make_shared<VirtualEdge>(ex->getFreq(), VN, VN, replaceEdges);
        }
        else
        {
            newEdge = make_shared<VirtualEdge>(ex->getFreq(), VN, ex->getWeightedSnk(), replaceEdges);
        }
        ///auto newEdge = make_shared<VirtualEdge>(ex->getFreq(), VN, ex->getWeightedSnk(), replaceEdges);
        EdgeToVE[ex].insert(newEdge);
        //newEdge->setWeight((uint64_t)((float)ex->getFreq() / ex->getWeight()))
        newEdge->setWeight((uint64_t)round(((float)ex->getFreq() / ex->getWeight())));
        graph.removeEdge(ex);
        graph.addEdge(newEdge);
        VN->addSuccessor(newEdge);
        ex->getWeightedSnk()->removePredecessor(ex);
        ex->getWeightedSnk()->addPredecessor(newEdge);
    }
    // finally, remove the virtualized nodes
    for (auto n : subgraph.getNodes())
    {
        graph.removeNode(n);
    }
    for( auto e : subgraph.getEdges() )
    {
        graph.removeEdge(e);
    }
    graph.addNode(VN);
}

/// @brief Virtualizes a function between the provided entrance and exit edge snk nodes
///
/// The provided subgraph is virtualized to one layer "above" the subgraph given (meaning all Virtual Nodes genenerated in this function map to exactly one underlying node)
/// The provided entrance and exits are not virtualized.
/// All virtual nodes generated here are put into the graph and the old nodes are removed.
/// The edges that lead into and out of the function subgraph are virtualized and their nodes are mapped to the virtualized nodes in the function and unvirtualized nodes outside the function.
/// The old edges are removed from the graph and the virtual edges take their place
/// @param graph    Input graph for the program
/// @param VN       Virtual node that will ultimately replace the subgraph in @subgraph
/// @param subgraph All nodes that describe the function to be virtualized. This may include any virtualized functions within the subgraph.
/// @param entrance ControlNode whose underlying basic block is the caller block. This node is not virtualized
/// @param exits    Set of edges which point to nodes that can be reached after a call to this function from the entrance parameter. The exit snk nodes are not virtualized.
set<std::shared_ptr<VirtualEdge>, GECompare> VirtualizeFunctionSubgraph(Graph &graph, const ControlGraph &funcGraph, const std::shared_ptr<CallEdge> &entrance, const set<std::shared_ptr<UnconditionalEdge>, GECompare> &exits)
{
    // each node in the subgraph has to be virtualized and given only the entrances and exits provided in the arguments to this function
    // two reasons:
    // first, when building a virtual node for this function call, when we call getEntrances(), getExits(), we only want the entrances and exits that make sense for this function inline
    // second, when reverse-transforming, we need our subgraph to refer back to the original nodes of the un-inlined function call
    set<std::shared_ptr<VirtualNode>, p_GNCompare> add;
    set<std::shared_ptr<VirtualEdge>, GECompare> addEdge;
    for (auto s : funcGraph.getControlNodes())
    {
        shared_ptr<VirtualNode> newSubVN = nullptr;
        newSubVN = make_shared<VirtualNode>();
        newSubVN->addNode(s);
        add.insert(newSubVN);
        NodeToVN[s].insert(newSubVN);
    }
    for (auto s : add)
    {
        // frequencies and probabilities need to be reset because of the entrance/exit partitioning problem
        // this partitioning will move edges around exclusively to virtual nodes, changing the probabilities they should have
        // this counter counts up the outgoing frequencies of a node, it is later used to calculate new probabilities
        uint64_t outgoingFreq = 0;
        // nodes may loop upon themselves, which won't show up in the entrances/exits logic
        // here we account for this
        for (const auto &e : s->getSubgraphEdges())
        {
            outgoingFreq += e->getFreq();
            set<shared_ptr<UnconditionalEdge>, GECompare> replaceEdges;
            replaceEdges.insert(e);
            // this code maps the old unvirtualized nodes to the virtualized nodes present in the "add" set
            shared_ptr<VirtualNode> VNpred = nullptr;
            shared_ptr<VirtualNode> VNsucc = nullptr;
            for (auto v : add)
            {
                if (v->getSubgraph().find(e->getWeightedSrc()) != v->getSubgraph().end())
                {
                    VNpred = v;
                }
                if (v->getSubgraph().find(e->getWeightedSnk()) != v->getSubgraph().end())
                {
                    VNsucc = v;
                }
            }
            if (!VNpred || !VNsucc)
            {
                throw CyclebiteException("Could not find a virtual node that represents a node in the function subgraph!");
            }
            auto newEdge = make_shared<VirtualEdge>(e->getFreq(), VNpred, VNsucc, replaceEdges);
            EdgeToVE[e].insert(newEdge);
            addEdge.insert(newEdge);
            s->addPredecessor(newEdge);
            VNpred->addSuccessor(newEdge);
            graph.addEdge(newEdge);
        }
        // virtual subgraph nodes are only allowed to have edges that stay within the subgraph, entrances or exits for this function inlining
        // this loop maps the edges currently in the graph to a virtual edge that points to the virtual nodes (made in the above loop) of the new function subgraph
        for (auto p : s->getEntrances())
        {
            // subgraph node predecessor edges can either be entirely within the subgraph
            if (funcGraph.find(p->getWeightedSrc()))
            {
                if (funcGraph.find(p->getWeightedSnk()))
                {
                    set<shared_ptr<UnconditionalEdge>, GECompare> replaceEdges;
                    replaceEdges.insert(p);
                    shared_ptr<VirtualNode> VNpred = nullptr;
                    for (auto v : add)
                    {
                        if (v->getSubgraph().find(p->getWeightedSrc()) != v->getSubgraph().end())
                        {
                            VNpred = v;
                        }
                    }
                    if (!VNpred)
                    {
                        throw CyclebiteException("Could not find a virtual node that represents a node in the function subgraph!");
                    }
                    auto newEdge = make_shared<VirtualEdge>(p->getFreq(), VNpred, s, replaceEdges);
                    EdgeToVE[p].insert(newEdge);
                    addEdge.insert(newEdge);
                    s->addPredecessor(newEdge);
                    VNpred->addSuccessor(newEdge);
                    graph.addEdge(newEdge);
                }
            }
            // or they can be the entrance edge of the entrance designated for this function inlining
            // in this case we want to link the edge between the virtual node successor and a predecessor node that is outside the function subgraph, thus we use the node that is encoded in the old edge
            else if (p->getWeightedSrc() == entrance->getWeightedSrc() )
            {
                set<shared_ptr<UnconditionalEdge>, GECompare> replaceEdges;
                replaceEdges.insert(p);
                auto newEdge = make_shared<VirtualEdge>(p->getFreq(), p->getWeightedSrc(), s, replaceEdges);
                newEdge->setWeight((uint64_t)((float)p->getFreq() / p->getWeight()));
                EdgeToVE[p].insert(newEdge);
                addEdge.insert(newEdge);
                s->addPredecessor(newEdge);
                p->getWeightedSrc()->addSuccessor(newEdge);
                // we remove the edge that connects the old node to the outside exit, it is now "underneath" the new virtual edge
                p->getWeightedSrc()->removeSuccessor(p);
                graph.removeEdge(p);
                graph.addEdge(newEdge);
                // there is a corner case with indirect recursion where if the entrance to the indirect recursion was a function pointer that took on multiple values, the outgoing edge weights will sum to a number greater than one
                // this is because the entrance to the indirect recursion will have outgoing edges added to it... the number of entrances to the indirect recursion
                // so here we just re-normalized the outgoing edge probabilities
                uint64_t sum = 0;
                for (const auto &succ : p->getWeightedSrc()->getSuccessors())
                {
                    sum += succ->getFreq();
                }
                for (auto &succ : p->getSrc()->getSuccessors())
                {
                    if (auto ce = dynamic_pointer_cast<ConditionalEdge>(succ))
                    {
                        ce->setWeight(sum);
                    }
                }
            }
        }
        for (auto succ : s->getExits())
        {
            // the src node of the edge must be inside the subgraph
            if (funcGraph.find(succ->getWeightedSrc()) )
            {
                // the sink node can either be inside the subgraph
                if (funcGraph.find(succ->getWeightedSnk()))
                {
                    set<shared_ptr<UnconditionalEdge>, GECompare> replaceEdges;
                    replaceEdges.insert(succ);
                    shared_ptr<VirtualNode> VNsucc = nullptr;
                    for (auto v : add)
                    {
                        if (v->getSubgraph().find(succ->getWeightedSnk()) != v->getSubgraph().end())
                        {
                            VNsucc = v;
                        }
                    }
                    if (!VNsucc)
                    {
                        throw CyclebiteException("Could not find a virtual node that represents a node in the function subgraph!");
                    }
                    outgoingFreq += succ->getFreq();
                    auto newEdge = make_shared<VirtualEdge>(succ->getFreq(), s, VNsucc, replaceEdges);
                    EdgeToVE[succ].insert(newEdge);
                    addEdge.insert(newEdge);
                    newEdge->setWeight(succ->getFreq());
                    s->addSuccessor(newEdge);
                    VNsucc->addPredecessor(newEdge);
                    graph.addEdge(newEdge);
                }
                // or it can be in one of the exits that this inline transform has
                // in this case we want to link the edge between a virtual node predecessor and a successor that is outside the function subgraph, thus we use the node that is encoded in the old edge
                else if (exits.find(succ) != exits.end())
                {
                    outgoingFreq += succ->getFreq();
                    set<shared_ptr<UnconditionalEdge>, GECompare> replaceEdges;
                    replaceEdges.insert(succ);
                    auto newEdge = make_shared<VirtualEdge>(succ->getFreq(), s, succ->getWeightedSnk(), replaceEdges);
                    EdgeToVE[succ].insert(newEdge);
                    addEdge.insert(newEdge);
                    s->addSuccessor(newEdge);
                    succ->getWeightedSnk()->addPredecessor(newEdge);
                    // we remove the edge that connects the old node to the outside exit, it is now "underneath" the new virtual edge
                    succ->getWeightedSnk()->removePredecessor(succ);
                    graph.removeEdge(succ);
                    graph.addEdge(newEdge);
                }
            }
        }
        for (auto succ : s->getSuccessors())
        {
            if (auto cond = dynamic_pointer_cast<ConditionalEdge>(succ))
            {
                cond->setWeight(outgoingFreq);
            }
            else
            {
                set<shared_ptr<UnconditionalEdge>, GECompare> vEdges;
                vEdges.insert(succ);
                auto newS = make_shared<VirtualEdge>(outgoingFreq, succ->getWeightedSrc(), succ->getWeightedSnk(), vEdges);
                newS->setWeight(outgoingFreq);
                auto src = succ->getSrc();
                auto snk = succ->getSnk();
                src->removeSuccessor(succ);
                src->addSuccessor(newS);
                snk->removePredecessor(succ);
                snk->addPredecessor(newS);
                graph.addEdge(succ);
                graph.removeEdge(newS);
            }
        }
        graph.addNode(s);
    }
    return addEdge;
}

void Cyclebite::Graph::reverseTransform(Graph &graph)
{
    // algorithm runs until no virtual nodes are left
    bool virt = true;
    while (virt)
    {
        virt = false;
        vector<std::shared_ptr<GraphNode>> tmpNodes(graph.nodes().begin(), graph.nodes().end());
        for (auto node : tmpNodes)
        {
            if (auto VN = dynamic_pointer_cast<VirtualNode>(node))
            {
                virt = true;
                // a virtual node can be unwound if it satisfies two conditions
                // 1. entrance edges are either not virtual, or are virtual and their underlying edges have their src node in the graph
                //   - this is because VirtualizeSubgraph points an existing node to the virtual node, thus entrances whose src nodes are in the graph point to a node that has most recently been virtualized, preserving order of transform
                // 2. exit nodes are either not virtual, or are virtual and their underlying edges have their snk node in the graph
                //   - this is because VirtualizeSubgraph points the virtual node to an existing node in the graph, thus exits whose snk nodes are in the  graph point to from a node that has most recently been virtualized, preserving order of transform
                // this boolean is set to true initially, and if any entrance, exit edges do not meet the criteria above, it is set to false and the node subgraph is not unwound
                bool allPass = true;
                for (auto ent : VN->getEntrances())
                {
                    // if the entrance edge is virtual, get its underlying edge and make sure that its src node is still in the graph
                    if (auto Ven = dynamic_pointer_cast<VirtualEdge>(ent))
                    {
                        for (auto old : Ven->getEdges())
                        {
                            if (!graph.find(old->getWeightedSrc()))
                            {
                                // we cannot find the source node of this entrance in the graph, so we can't be sure that this is an entrance to a virtual node recently transformed, fail
                                allPass = false;
                            }
                        }
                    }
                }
                for (auto ex : VN->getExits())
                {
                    // if the exit edge is virtual, get its underlying edge and make sure that its src node is still in the graph
                    if (auto Vex = dynamic_pointer_cast<VirtualEdge>(ex))
                    {
                        for (auto old : Vex->getEdges())
                        {
                            if (!graph.find(old->getWeightedSnk()))
                            {
                                // we cannot find the source node of this exit in the graph, so we can't be sure that this is an exit to a virtual node recently transformed, fail
                                allPass = false;
                            }
                        }
                    }
                    else
                    {
                        // this means that the source of this entrance was never virtualized, therefore we can be sure this edge was virtualized most recently
                    }
                }
                if (allPass)
                {
                    // all entrances, exits satisfy the criteria, so unwind the virtual node
                    // two steps:
                    // 1. get rid of virtual entrance/exit edges and put their underlyings back into the edges set
                    // 2. get rid of virtual node and puts its subgraph back into the nodes set
                    for (auto ent : VN->getEntrances())
                    {
                        if (auto VE = dynamic_pointer_cast<VirtualEdge>(ent))
                        {
                            graph.removeEdge(VE);
                        }
                        else
                        {
                            // do nothing, the edge has no underlyings and already points to the correct nodes
                        }
                    }
                    for (auto ex : VN->getExits())
                    {
                        if (auto VE = dynamic_pointer_cast<VirtualEdge>(ex))
                        {
                            graph.addEdges(EdgeConvert(VE->getEdges()));
                            graph.removeEdge(VE);
                        }
                        else
                        {
                            // do nothing, the edge has no underlyings and already points to the correct nodes
                        }
                    }
                    graph.addNodes(Cyclebite::Graph::NodeConvert(VN->getSubgraph()));
                    graph.removeNode(VN);
                }
            }
        }
    }
}

ControlGraph Cyclebite::Graph::reverseTransform_MLCycle(const ControlGraph& graph)
{
    auto newGraph = graph;
    // transform the graph until all parent-most-level mlcycles are exposed
    bool mlCycleFound = true;
    while (mlCycleFound)
    {
        mlCycleFound = false;
        vector<std::shared_ptr<GraphNode>> tmpNodes(newGraph.nodes().begin(), newGraph.nodes().end());
        for (auto node : tmpNodes)
        {
            if( dynamic_pointer_cast<MLCycle>(node) == nullptr )
            {
                if (auto VN = dynamic_pointer_cast<VirtualNode>(node) )
                {
                    deque<shared_ptr<VirtualNode>> Q;
                    set<shared_ptr<VirtualNode>> covered;
                    Q.push_front(VN);
                    while( !Q.empty() )
                    {
                        for( const auto& node : Q.front()->getSubgraph() )
                        {
                            if( auto mlc = dynamic_pointer_cast<MLCycle>(node) )
                            {
                                mlCycleFound = true;
                                break;
                            }
                            if( auto vn = dynamic_pointer_cast<VirtualNode>(node) )
                            {
                                Q.push_back(vn);
                            }
                        }
                        if( mlCycleFound )
                        {
                            break;
                        }
                        Q.pop_front();
                    }
                    if( mlCycleFound )
                    {
                        // two steps:
                        // 1. get rid of virtual entrance/exit edges and put their underlyings back into the edges set
                        // 2. get rid of virtual node and puts its subgraph back into the nodes set
                        for (auto ent : VN->getPredecessors())
                        {
                            if (auto VE = dynamic_pointer_cast<VirtualEdge>(ent))
                            {
                                newGraph.removeEdge(VE);
                            }
                            else
                            {
                                // do nothing, the edge has no underlyings and already points to the correct nodes
                            }
                        }
                        for (auto ex : VN->getSuccessors())
                        {
                            if (auto VE = dynamic_pointer_cast<VirtualEdge>(ex))
                            {
                                newGraph.addEdges(EdgeConvert(VE->getEdges()));
                                newGraph.removeEdge(VE);
                            }
                            else
                            {
                                // do nothing, the edge has no underlyings and already points to the correct nodes
                            }
                        }
                        newGraph.addNodes(Cyclebite::Graph::NodeConvert(VN->getSubgraph()));
                        newGraph.addEdges(Cyclebite::Graph::EdgeConvert(VN->getSubgraphEdges()));
                        newGraph.removeNode(VN);
                    }
                }
            }
        }
    }
    return newGraph;
}

ControlGraph Cyclebite::Graph::TrivialTransforms(const std::shared_ptr<ControlNode> &sourceNode)
{
    ControlGraph subgraph;
    auto source = sourceNode;
    auto sink   = source;
    bool inserted = true;
    while( inserted )
    {
        inserted = false;
        // a trivial node merge must satisfy two conditions
        // 1.) The source node has exactly 1 neighbor with certain probability
        // 2.) The sink node has exactly 1 predecessor (the source node) with certain probability
        // 3.) The edge connecting source and sink must not cross a context level i.e. source and sink must belong to the same function
        // combine all trivial edges
        // first condition, our source node must have 1 certain successor
        if ((source->getSuccessors().size() == 1) && ((*(source->getSuccessors().begin()))->getWeight() > 0.9999))
        {
            auto sink = (*source->getSuccessors().begin())->getWeightedSnk();
            // second condition, the sink node must have 1 certain predecessor
            if ((sink->getPredecessors().size() == 1) && (sink->getSuccessors().size() == 1) && (source->isPredecessor(sink)) && (sink->isSuccessor(source)) && ((*(sink->getPredecessors().begin()))->getWeight() > 0.9999))
            {
                // third condition, sink node is not allowed to loop back to source node
                if (!sink->isPredecessor(source))
                {
                    // fourth condition: both source and sink must have at least one pred and one succ
                    if( (!source->getPredecessors().empty()) && (!source->getSuccessors().empty()) && (!sink->getPredecessors().empty()) && (!sink->getSuccessors().empty()) )
                    {
                        subgraph.addNode(source);
                        subgraph.addNode(sink);
                        subgraph.addEdge(*(source->getSuccessors().begin()));
                        source = sink;
                        inserted = true;
                    }
                }
            }
        }
    }
    return subgraph;
}

ControlGraph Cyclebite::Graph::BranchToSelectTransforms(const ControlGraph &graph, const shared_ptr<ControlNode> &source)
{
    ControlGraph subgraph;
    // Vocabulary
    // entrance - first node that will execute in the target subgraph
    // midnodes - nodes that lie between entrance and exit
    // exit     - last node that will execute in the target subgraph
    // Rules
    // 1.) The subgraph must have exactly one entrance and one exit
    // 2.) Exactly one layer of midnodes must exist between entrance and exit. The entrance is allowed to lead directly to the exit
    // 3.) No cycles may exist in the subgraph i.e. Flow can only go from entrance to zero or one midnode to exit
    // 4.) No subgraph edges can cross context levels i.e. the entire subgraph must be contained in one function
    // block that may be an exit of a transformable subgraph
    std::shared_ptr<ControlNode> potentialExit = nullptr;

    // trivial check: source node must have predecessors and successors
    if( (source->getPredecessors().empty()) || (source->getSuccessors().empty()) )
    {
        return subgraph;
    }

    // first step, acquire middle nodes
    // we do this by treating the current block as the entrance to a potential subgraph and exploring its successors and successors of successors
    set<std::shared_ptr<GraphNode>, p_GNCompare> midNodes;
    // also, check for case 1 of case 2 configurations
    // flag representing the case we have, if any.
    // false for Case 1, true for Case 2
    bool MergeCase = false;
    // 1.) 0-deep branch->select: entrance can go directly to exit
    // 2.) 1-deep branch->select: entrance cannot go directly to exit
    // holds all successors of all midnodes
    std::set<uint64_t> midNodeSuccessors;
    for (const auto &midNode : source->getSuccessors())
    {
        midNodes.insert(midNode->getWeightedSnk());
        if (graph.find(midNode->getWeightedSnk()))
        {
            for (const auto &neighbor : graph.getNode(midNode->getWeightedSnk())->getSuccessors())
            {
                midNodeSuccessors.insert(neighbor->getWeightedSnk()->ID());
            }
        }
        else
        {
            throw CyclebiteException("Found a midnode that is not in the control flow graph!");
        }
    }
    if (midNodeSuccessors.size() == 1) // corner case where the exit of the subgraph has no successors (it is the last node to execute in the program). In this case we have to check if the entrance is a predecessor of the lone midNodeTarget
    {
        if (graph.find_node(*midNodeSuccessors.begin()))
        {
            auto cornerCase = graph.getNode(*midNodeSuccessors.begin());
            if (cornerCase->isSuccessor(source))
            {
                // the entrance can lead directly to the exit
                MergeCase = false;
                potentialExit = cornerCase;
                midNodes.erase(potentialExit);
            }
            else
            {
                // we have confirmed case 2: all entrance successors lead to a common exit, meaning the entrance cannot lead directly to the exit
                MergeCase = true;
                potentialExit = graph.getNode(*midNodeSuccessors.begin());
            }
        }
        else
        {
            throw CyclebiteException("Could not find midNode successor in control flow graph!");
        }
    }
    // else we may have a neighbor of the entrance that is the exit
    // to find the exit we need to find a neighbor of the entrance which is the lone successor of all other successors of the entrance
    else
    {
        if (source->getSuccessors().size() > 1)
        {
            for (auto succ : source->getSuccessors())
            {
                bool common = true;
                for (auto neighbor : source->getSuccessors())
                {
                    if (succ == neighbor)
                    {
                        continue;
                    }
                    for (const auto &succ2 : neighbor->getWeightedSnk()->getSuccessors())
                    {
                        if (succ2->getWeightedSnk() != succ->getWeightedSnk())
                        {
                            common = false;
                        }
                    }
                }
                if (common)
                {
                    potentialExit = succ->getWeightedSnk();
                    midNodes.erase(succ->getWeightedSnk());
                    break;
                }
            }
        }

    }
    if (potentialExit == nullptr)
    {
        return subgraph;
    }
    else if( (potentialExit->getPredecessors().empty()) || (potentialExit->getSuccessors().empty()) )
    {
        return subgraph;
    }
    // in order for either case to be true, six conditions must be checked
    // 1.) the entrance can't have the exit or any midnodes as predecessors
    auto tmpMids = midNodes;
    auto pushed = tmpMids.insert(potentialExit);
    if (!pushed.second)
    {
        // somehow the exit ID is still in the midNodes set
        return subgraph;
    }
    for (auto &pred : source->getPredecessors())
    {
        tmpMids.erase(pred->getWeightedSrc());
    }
    if (tmpMids.size() != midNodes.size() + 1)
    {
        return subgraph;
    }
    // 2.) all midnodes must only have entrance as its lone predecessor
    bool badCondition = false; // can be flipped either by a bad midNode pred or a missing midNode from the nodes set
    for (const auto &mid : midNodes)
    {
        if ((mid->getPredecessors().size() != 1) || (!mid->isSuccessor(source)))
        {
            badCondition = true;
        }
    }
    if (badCondition)
    {
        return subgraph;
    }
    // 3.) all midnodes must only have potentialExit as its lone successor
    badCondition = false; // can be flipped either by a bad midNode pred or a missing midNode from the nodes set
    for (const auto &mid : midNodes)
    {
        if ((mid->getSuccessors().size() != 1) || (!mid->isPredecessor(potentialExit)))
        {
            badCondition = true;
        }
    }
    if (badCondition)
    {
        return subgraph;
    }
    // 5.) potentialExit can't have the entrance or any midnodes as successors
    tmpMids = midNodes;
    badCondition = false; // flipped if we find a midnode or entrance in potentialExit successors, or we find a bad node
    for (const auto &k : potentialExit->getSuccessors())
    {
        if (midNodes.find(k->getWeightedSnk()) != midNodes.end())
        {
            badCondition = true;
            return subgraph;
        }
    }
    if (badCondition)
    {
        return subgraph;
    }
    // Now we do case-specific checks
    if (MergeCase)
    {
        // case 2: the entrance cannot lead directly to the exit
        // 2 conditions must be checked
        // 1.) entrance only has midnodes as successors
        tmpMids = midNodes;
        for (auto &n : source->getSuccessors())
        {
            tmpMids.erase(n->getWeightedSnk());
        }
        if (!tmpMids.empty())
        {
            return subgraph;
        }
        // 2.) The only successor of the midnodes is potentialExit
        set<std::shared_ptr<GraphNode>, p_GNCompare> tmpPreds;
        for (auto &p : potentialExit->getPredecessors())
        {
            tmpPreds.insert(p->getWeightedSrc());
        }
        for (auto m : midNodes)
        {
            tmpPreds.erase(m);
        }
        if (!tmpPreds.empty())
        {
            return subgraph;
        }
    }
    else
    {
        // case 1: the entrance can lead directly to the exit
        // 2 conditions must be checked
        // 1.) entrance only has midnodes and potentialExit as successors
        tmpMids = midNodes;
        tmpMids.insert(potentialExit);
        for (auto &n : source->getSuccessors())
        {
            tmpMids.erase(n->getWeightedSnk());
        }
        if (!tmpMids.empty())
        {
            return subgraph;
        }
        // 2.) The midnodes and entrance only have potentialExit as their successor
        set<std::shared_ptr<GraphNode>, p_GNCompare> tmpPreds;
        for (auto p : potentialExit->getPredecessors())
        {
            tmpPreds.insert(p->getWeightedSrc());
        }
        tmpMids = midNodes;
        tmpMids.insert(source);
        for (auto &n : tmpMids)
        {
            tmpPreds.erase(n);
        }
        if (!tmpPreds.empty())
        {
            return subgraph;
        }
    }
    for (const auto& mid : midNodes)
    {
        subgraph.addNode(mid);
        for( const auto& pred : mid->getPredecessors() )
        {
            subgraph.addEdge(pred);
        }
        for( const auto& succ : mid->getSuccessors() )
        {
            subgraph.addEdge(succ);
        }
    }
    subgraph.addNode(source);
    for( const auto& succ : source->getSuccessors() )
    {
        subgraph.addEdge(succ);
    }
    subgraph.addNode(potentialExit);
    for( const auto& pred : potentialExit->getPredecessors() )
    {
        subgraph.addEdge(pred);
    }
    return subgraph;
}

/// @brief Evaluates a subgraph for its entrances and exits, and returns true if the entrance and exit are the bottlenecks of the subgraph
///
/// @param subgraph Input subgraph to evaluate. This subgraph cannot contain any cycles, and there must be a path between source and sink. This parameter is passed by reference and may be manipulated if the function returns true
/// @param source   The intended source node of the subgraph. A source node of the subgraph should have all its predecessors outside the subgraph and all its successors within the subgraph
/// @param sink     The intended sink node of the subgraph. A sink node of the subgraph should have all its predecessors within the subgraph and all its successors outside
/// @retval         True if the input subgraph can only be entered into through source and only exited through sink
bool Cyclebite::Graph::FanInFanOutTransform(ControlGraph &subgraph, const std::shared_ptr<ControlNode> &source, const std::shared_ptr<ControlNode> &sink)
{
    // Checks
    // 1. the subgraph is more than just source and sink node
    if (subgraph.node_count() < 3)
    {
        return false;
    }
    // 2. Every node has at least one predecessor and at least one successor
    for (const auto &node : subgraph.nodes())
    {
        if (node->getSuccessors().empty())
        {
            return false;
        }
        else if (node->getPredecessors().empty())
        {
            return false;
        }
    }
    // 3. The only entrance to the subgraph is through source and the only exit from the subgraph is through sink
    // Removes
    // 1. Any node (except the sink) that has 0 successors in the subgraph
    set<std::shared_ptr<GraphNode>, p_GNCompare> toRemove;
    for (const auto &node : subgraph.nodes())
    {
        if (node == source)
        {
            for (const auto &succ : source->getSuccessors())
            {
                if (!subgraph.find(succ))
                {
                    return false;
                }
            }
        }
        else if (node == sink)
        {
            for (const auto &pred : sink->getPredecessors())
            {
                if (!subgraph.find(pred))
                {
                    return false;
                }
            }
        }
        else
        {
            for (const auto &pred : node->getPredecessors())
            {
                if (!subgraph.find(pred))
                {
                    return false;
                }
            }
            bool succInSubgraph = false;
            for (const auto &nei : node->getSuccessors())
            {
                if (!subgraph.find(nei))
                {
                    return false;
                }
                else
                {
                    succInSubgraph = true;
                }
            }
            if (!succInSubgraph)
            {
                toRemove.insert(node);
            }
        }
    }
    for (const auto &n : toRemove)
    {
        subgraph.removeNode(n);
        for (const auto &succ : n->getSuccessors())
        {
            subgraph.removeEdge(succ);
        }
        for (const auto &pred : n->getPredecessors())
        {
            subgraph.removeEdge(pred);
        }
    }
    return true;
}

/// Detects recursion, either direct or indirect
bool Cyclebite::Graph::hasDirectRecursion(const llvm::CallGraphNode *node)
{
    for (auto f = node->begin(); f != node->end(); f++)
    {
        if (f->second->getFunction() == node->getFunction())
        {
            return true;
        }
    }
    return false;
}

bool hasDirectRecursion(const std::shared_ptr<ControlNode> &node, const std::map<int64_t, const llvm::BasicBlock *> &IDToBlock, const llvm::CallGraph &CG)
{
    auto block = NodeToBlock(node, IDToBlock);
    if (block->getParent())
    {
        auto CGentry = CG[block->getParent()];
        return hasDirectRecursion(CGentry);
    }
    else
    {
        throw CyclebiteException("Could not map a function block to a node during simple recursion evaluation!");
    }
}

bool Cyclebite::Graph::hasIndirectRecursion(const Cyclebite::Graph::CallGraph &graph, const shared_ptr<Cyclebite::Graph::CallGraphNode> &node)
{
    auto cycle = Dijkstras(graph, node->ID(), node->ID());
    if (cycle.size() > 1)
    {
        // the loop had more than one node in it, so it must be indirect recursion
        return true;
    }
    else if (cycle.size() == 1)
    {
        // this function is direct recursive and might be indirect recursive
        // we have to take the direct recursion edge out and redo the analysis
        auto copy = graph;
        auto finderEdge = make_shared<UnconditionalEdge>(0, node, node);
        copy.removeEdge(finderEdge);
        auto cycle2 = Dijkstras(copy, node->ID(), node->ID());
        if (cycle2.size() > 1)
        {
            return true;
        }
        else
        {
            return false;
        }
    }
    else
    {
        return false;
    }
}

/// Returns true if this function has direct recursion and false otherwise. If the input function is indirect and direct recursive, the return value will be true
bool Cyclebite::Graph::hasDirectRecursion(const Cyclebite::Graph::CallGraph &graph, const shared_ptr<Cyclebite::Graph::CallGraphNode> &src)
{
    auto cycle = Dijkstras(graph, src->ID(), src->ID());
    if (cycle.size() == 1)
    {
        return true;
    }
    else
    {
        return false;
    }
}

/// Carries out a depth-first search of the callgraph
bool Cyclebite::Graph::hasIndirectRecursion(const llvm::CallGraphNode *node)
{
    set<const llvm::CallGraphNode *> visited;
    deque<const llvm::CallGraphNode *> Q;
    Q.push_back(node);
    while (!Q.empty())
    {
        visited.insert(Q.front());
        bool pushedNeighbor = false;
        for (auto f = Q.front()->begin(); f != Q.front()->end(); f++)
        {
            // this code checks if we have found a "backedge" in the callgraph (because any entry in the queue is above the current in the DFS)
            auto entry = std::find(Q.begin(), Q.end(), f->second);
            if (entry != Q.end())
            {
                // but we are only looking for backedges whose source node is not the same as the sink node (ie indirect recursion vs direct recursion)
                if (((*entry)->getFunction() == f->second->getFunction()) && (f->second->getFunction() != Q.front()->getFunction()))
                {
                    if (f->second->getFunction() == node->getFunction())
                    {
                        return true;
                    }
                }
            }
            if (visited.find(f->second) == visited.end())
            {
                Q.push_front(f->second);
                pushedNeighbor = true;
                break;
            }
        }
        if (!pushedNeighbor)
        {
            Q.pop_front();
        }
    }
    return false;
}

bool hasIndirectRecursion(const std::shared_ptr<ControlNode> &node, const std::map<int64_t, const llvm::BasicBlock *> &IDToBlock, const llvm::CallGraph &CG)
{
    auto block = NodeToBlock(node, IDToBlock);
    if (block->getParent())
    {
        auto CGentry = CG[block->getParent()];
        return hasIndirectRecursion(CGentry);
    }
    else
    {
        throw CyclebiteException("Could not map a function block to a node during indirect recursion evaluation!");
    }
}

set<shared_ptr<CallGraphNode>, p_GNCompare> getIndirectRecursionCycle(const Cyclebite::Graph::CallGraph &graph, const shared_ptr<Cyclebite::Graph::CallGraphNode> &CGN)
{
    /* john 6/29/22
     * the analysis should have to make it back to the original parent, so put a check in the cycles involved in a new child 
     * if i am capturing a recursive of a subgraph in the call graph
     *  - all paths must lead back to the current node through the new child node
     *  - if this is not true, this likely indicates that a higher level of recursion exists, and that higher level should be the inline candidate
     * In a call graph, if you start jumping above the caller context, things go wrong
     * 
     * Watermark approach (comes from Plato's cave)
     *  - 0: My dependencies are currently met ("easy" case... a depends on a)
     *  - 1: I am allowed to merge all current dependencies once, then ask: if I evaluate this set, do I meet all dependencies for each member in the set?
     *  -> then keep recursing through this until all dependencies are met (number represents the number of set unions that would be required to find all dependencies)
     *  -> threshold should probably be around 10, because an example that embeds 10 callgraph cycles together is interesting
     */
    set<shared_ptr<CallGraphNode>, p_GNCompare> cycle;
    // do a BFS of the subgraph in the CGN
    // for each node in the BFS, do Dijkstras
    // if Dijkstra returns a cycle keep the node
    // else its not part of "the" cycle
    set<shared_ptr<CallEdge>, GECompare> embeddedFunctions;
    set<shared_ptr<Cyclebite::Graph::CallGraphNode>> covered;
    deque<shared_ptr<Cyclebite::Graph::CallGraphNode>> Q;
    Q.push_front(CGN);
    // this copy has direct recursion edges removed from it as the analysis progresses
    // this prevents the case where a function that is both direct and indirect recursive is erroneously categorized by Dijkstra as direct recursive only
    auto graphCopy = graph;
    while (!Q.empty())
    {
        for (const auto &child : Q.front()->getChildren())
        {
            if (covered.find(child->getChild()) == covered.end())
            {
                // recursion entrances are not children
                if (child->getChild() != Q.front())
                {
                    // see if this node is part of a cycle
                    auto childCycle = Dijkstras(graphCopy, child->getChild()->ID(), child->getChild()->ID());
                    if (childCycle.size() > 1)
                    {
                        cycle.insert(static_pointer_cast<CallGraphNode>(graphCopy.getOriginalNode(child->getChild()->ID())));
                        Q.push_back(child->getChild());
                        covered.insert(child->getChild());
                    }
                    else if (childCycle.size() == 1)
                    {
                        auto copy = graphCopy;
                        auto finderEdge = make_shared<UnconditionalEdge>(0, child->getChild(), child->getChild());
                        copy.removeEdge(finderEdge);
                        auto childCycle2 = Dijkstras(copy, child->getChild()->ID(), child->getChild()->ID());
                        if (childCycle2.size() > 1)
                        {
                            cycle.insert(static_pointer_cast<CallGraphNode>(graphCopy.getOriginalNode(child->getChild()->ID())));
                            Q.push_back(child->getChild());
                            covered.insert(child->getChild());
                            graphCopy = copy;
                        }
                    }
                }
            }
        }
        Q.pop_front();
    }
    return cycle;
}

/// @brief Returns all entrance edges to a cycle in the callgraph
///
/// This function will find a cycle in the callgraph that is entered into by CGN and get all edges that go into the cycle but whose source node cannot be returned to by the cycle
/// @param graph    Callgraph that contains CGN
/// @param CGN      CallGraphNode in question. This node should be part of a cycle in the callgraph. This method assumes this parameter is already known to be part of a cycle
/// @retval         All edges that can enter the cycle but are not themselves part of the cycle
set<shared_ptr<CallGraphEdge>, GECompare> getIndirectRecursionEntrances(const Cyclebite::Graph::CallGraph &graph, const shared_ptr<Cyclebite::Graph::CallGraphNode> &CGN)
{
    set<shared_ptr<CallGraphEdge>, GECompare> entrances;
    auto cycle = getIndirectRecursionCycle(graph, CGN);
    // now we look for all incoming edges into the cycle
    for (const auto &node : cycle)
    {
        for (const auto &pred : node->getParents())
        {
            if (cycle.find(pred->getParent()) == cycle.end())
            {
                entrances.insert(pred);
            }
        }
    }
    return entrances;
}

set<shared_ptr<CallGraphEdge>, GECompare> getDirectRecursionEntrances(const shared_ptr<Cyclebite::Graph::CallGraphNode> &CGN)
{
    set<shared_ptr<CallGraphEdge>, GECompare> entrances;
    // we are looking for calledges to this node that do not form a cycle
    for (const auto &pred : CGN->getParents())
    {
        if (pred->getParent() != pred->getChild())
        {
            entrances.insert(pred);
        }
    }
    return entrances;
}

set<shared_ptr<CallGraphEdge>, GECompare> FindEmbeddedFunctions(const Cyclebite::Graph::CallGraph &dynamicCG, const shared_ptr<Cyclebite::Graph::CallGraphNode> &CGN)
{
    set<shared_ptr<CallGraphEdge>, GECompare> embeddedFunctions;
    // there are two cases here
    // 1. the input function is indirect recursive. In this case we want the functions that do not belong on the cycle
    // 2. the input function is not indirect recursive. In this case we do a BFS of the children of the function, ignoring any possible direct recursion edges
    if (hasIndirectRecursion(dynamicCG, CGN))
    {
        auto idrCycle = getIndirectRecursionCycle(dynamicCG, CGN);
        for (const auto &node : idrCycle)
        {
            // bfs the function and add any children not in the cycle
            set<shared_ptr<Cyclebite::Graph::CallGraphNode>> covered;
            deque<shared_ptr<Cyclebite::Graph::CallGraphNode>> Q;
            Q.push_front(node);
            while (!Q.empty())
            {
                for (const auto &child : Q.front()->getChildren())
                {
                    if (covered.find(child->getChild()) == covered.end())
                    {
                        // recursion entrances are not children
                        if (child->getChild() != Q.front())
                        {
                            if (idrCycle.find(child->getChild()) == idrCycle.end())
                            {
                                embeddedFunctions.insert(child);
                                Q.push_back(child->getChild());
                                covered.insert(child->getChild());
                            }
                        }
                    }
                }
                Q.pop_front();
            }
        }
    }
    else
    {
        set<shared_ptr<Cyclebite::Graph::CallGraphNode>> covered;
        deque<shared_ptr<Cyclebite::Graph::CallGraphNode>> Q;
        Q.push_front(CGN);
        while (!Q.empty())
        {
            for (const auto &child : Q.front()->getChildren())
            {
                // recursion entrances are not embedded function edges
                if (child->getChild() != Q.front())
                {
                    // indirect recursive function edges are not embedded function edges
                    if (hasIndirectRecursion(dynamicCG, child->getChild()))
                    {
                        auto entrances = getIndirectRecursionEntrances(dynamicCG, child->getChild());
                        if (entrances.find(child) != entrances.end())
                        {
                            embeddedFunctions.insert(child);
                        }
                    }
                    else
                    {
                        embeddedFunctions.insert(child);
                    }
                }
                if (covered.find(child->getChild()) == covered.end())
                {
                    Q.push_back(child->getChild());
                    covered.insert(child->getChild());
                }
            }
            Q.pop_front();
        }
    }
    return embeddedFunctions;
}

deque<set<shared_ptr<CallGraphEdge>, GECompare>> ScheduleInlineTransforms(const Cyclebite::Graph::CallGraph &dynamicCG, const map<shared_ptr<Cyclebite::Graph::CallGraphNode>, set<shared_ptr<CallGraphEdge>, GECompare>, p_GNCompare> &embFunctions, const map<shared_ptr<Cyclebite::Graph::CallGraphNode>, set<shared_ptr<Cyclebite::Graph::CallGraphEdge>, GECompare>, p_GNCompare> &InlineCalls)
{
    /*
     * John 6/29/22
     * building the "who has no dependencies" pass is helpful, you just need to detect the deadlock case and handle it
     * You are trying to solve two problems as once
     *  - you are trying to schedule things in the right dependency order
     *  - you are also trying to detect the deadlock case, and this infusion is making the evaluation of the easy case harder
    */
    // now order the transforms such that each function has as many embedded functions inlined as possible before it is inlined
    deque<set<shared_ptr<CallGraphEdge>, GECompare>> InlineTransformQ;
    deque<shared_ptr<CallGraphNode>> Q;
    set<shared_ptr<CallGraphNode>> covered;
    Q.push_front(dynamicCG.getMainNode());
    // walk the callgraph where the inline candidates are
    uint32_t schedulingIterations = 0;
    while (!Q.empty())
    {
        if (covered.find(Q.back()) != covered.end())
        {
            Q.pop_back();
            continue;
        }
        bool allCovered = true;
        auto currentDependencies = embFunctions.at(Q.back());
        for (const auto &child : currentDependencies)
        {
            if ((covered.find(child->getChild()) == covered.end())) // && (accountedFor.find(child->getChild()) == accountedFor.end()) )
            {
                allCovered = false;
                break;
            }
        }

        if (allCovered)
        {
#ifdef DEBUG
            // sanity check. If it is time to push this to the Q, then all embedded functions should already be in the Q
            for (const auto &emb : currentDependencies)
            {
                bool found = false;
                auto inlineEntry = InlineCalls.find(emb->getChild());
                if (inlineEntry != InlineCalls.end())
                {
                    for (const auto &entry : InlineTransformQ)
                    {
                        if (entry.find(emb) != entry.end())
                        {
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                    {
                        throw CyclebiteException("Inlinable embedded function edge " + string(emb->getParent()->getFunction()->getName()) + " -> " + string(emb->getChild()->getFunction()->getName()) + " has not been scheduled yet for parent " + string(Q.back()->getFunction()->getName()) + "!");
                    }
                }
            }
#endif
            auto entry = InlineCalls.find(Q.back());
            if (entry != InlineCalls.end())
            {
#ifdef DEBUG
                string callGraphEdgeString = "";
                for (const auto &edge : entry->second)
                {
                    callGraphEdgeString += string(edge->getParent()->getFunction()->getName()) + " -> " + string(edge->getChild()->getFunction()->getName()) + ",";
                }
#endif
                // now combine indirect recursive subgraph edges into one entry in the schedule (the earliest one)
                // since the evaluation is called-function-centric, when the indirect recursive functions are scheduled, they get different entries in the schedule
                // but when evaluated for inlining, they will all get the same function subgraph (all functions involved in the indirect recursion)
                // when the inliner completes an inline, it will delete all the original nodes in the graph, ruining future inlines of the same indirect recursive function subgraph
                // to prevent this from happening, 
                if (hasIndirectRecursion(dynamicCG, Q.back()))
                {
                    bool found = false;
                    for (const auto &s : InlineTransformQ)
                    {
                        if (s == entry->second)
                        {
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                    {
#ifdef DEBUG
                        spdlog::info("Scheduling edges " + callGraphEdgeString + " to the inline queue for inlinable indirect-recursive function " + string(Q.back()->getFunction()->getName()));
#endif
                        InlineTransformQ.push_back(entry->second);
                    }
                }
                else
                {
#ifdef DEBUG
                    spdlog::info("Scheduling edges " + callGraphEdgeString + " to the inline queue for inlinable function " + string(Q.back()->getFunction()->getName()));
#endif
                    InlineTransformQ.push_back(entry->second);
                }
                covered.insert(Q.back());
                Q.pop_back();
            }
            else
            {
                covered.insert(Q.back());
            }
        }
        else
        {
            for (const auto &dep : currentDependencies)
            {
                if ((covered.find(dep->getChild()) == covered.end()))
                {
                    if (std::find(Q.begin(), Q.end(), dep->getChild()) == Q.end())
                    {
                        // the one-child-at-a-time push makes the search depth-first
                        Q.push_back(dep->getChild());
                        break;
                    }
                }
            }
        }
        if (++schedulingIterations > 100000)
        {
            throw CyclebiteException("Inline scheduling algorithm iteration is greater than 100,000 with function " + string(Q.back()->getFunction()->getName()) + " at the back!");
        }
    }
#ifdef DEBUG
    // check to see if we terminated after everything was done
    for (const auto &call : InlineCalls)
    {
        for (const auto &edge : call.second)
        {
            bool found = false;
            for (const auto &s : InlineTransformQ)
            {
                if (s.find(edge) != s.end())
                {
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                throw CyclebiteException("Function inline schedule does not include inlinable callgraphedge from parent " + string(edge->getParent()->getFunction()->getName()) + " to child " + string(edge->getParent()->getFunction()->getName()) + "!");
            }
        }
    }
    spdlog::info("Done scheduling " + to_string(InlineTransformQ.size()) + " inlinable function edges.");
#endif
    return InlineTransformQ;
}

void Cyclebite::Graph::VirtualizeSharedFunctions(ControlGraph &graph, const Cyclebite::Graph::CallGraph &dynamicCG)
{
    map<shared_ptr<Cyclebite::Graph::CallGraphNode>, set<shared_ptr<CallGraphEdge>, GECompare>, p_GNCompare> embFunctions;
    // set of nodes that are virtualized during the function inlining process
    // these nodes are removed after all function inlining is done
    set<shared_ptr<VirtualEdge>, GECompare> virtualizedEdges;
    // order transforms such that we do indirect recursion, then direct recursion, then ascending order of embedded function call count
    map<int, set<ControlNode *>> orderScore;
    // for some unknown reason, if this data structure uses shared pointers in its template, it breaks the c++ debugger
    map<shared_ptr<Cyclebite::Graph::CallGraphNode>, set<shared_ptr<Cyclebite::Graph::CallGraphEdge>, GECompare>, p_GNCompare> InlineCalls;

    // for each function (a node in the dynamic callgraph is a function known to have been exercised by the program)
    for (const auto &node : dynamicCG.getCallNodes())
    {
        // find the child function calls of this function, it will be used later to order function inlines (transform is bottom-up in the callgraph, thus children go first [special case: indirect recursion])
        embFunctions[node] = FindEmbeddedFunctions(dynamicCG, node);
        
        // count number of function calls to this node
        // when counting entrances, we are only interested in calls from outsiders (ie no recursive entrances)
        set<shared_ptr<CallGraphEdge>, GECompare> entrances;
        if (hasIndirectRecursion(dynamicCG, node))
        {
            entrances = getIndirectRecursionEntrances(dynamicCG, node);
        }
        else if (hasDirectRecursion(dynamicCG, node))
        {
            // only take the edges that enter from outside the recursive function
            entrances = getDirectRecursionEntrances(node);
        }
        else
        {
            for (const auto &parent : node->getParents())
            {
                entrances.insert(parent);
            }
        }
        // counts function callsites
        int entranceEdges = 0;
        for (const auto &e : entrances)
        {
            entranceEdges += e->getCallEdges().size();
        }
        // find all inline candidates (functions with more than one non-recursive callsite) and map that inline candidate to its entrances
        if (entranceEdges > 1)
        {
            InlineCalls[node] = entrances;
        }
    }
    if (InlineCalls.empty())
    {
        // no functions to inline
        return;
    }

    auto InlineTransformQ = ScheduleInlineTransforms(dynamicCG, embFunctions, InlineCalls);
    // for each set of callsites [each set is all callsites of a single function], generate a function subgraph using a random entry from the callsites set, prune that graph (according to what is possible at that callsite), and inline it
    for (const auto &cs : InlineTransformQ)
    {
        if (cs.empty())
        {
            // everything has been inlined, move on
            continue;
        }
        // a token edge is just a random entrance we use to generate a function subgraph for this inline candidate
        // later this subgraph will be tailored to the callsite it is inlined at (see "removeUnreachableNodes()")
        auto tokenEdge = *cs.begin();
        // generate a subgraph for this function that includes all child functions within it that have already been inlined
        ControlGraph funcGraph;
        if (hasIndirectRecursion(dynamicCG, tokenEdge->getChild()))
        {
            funcGraph = IndirectRecursionFunctionBFS(*tokenEdge->getCallEdges().begin());
        }
        else if (hasDirectRecursion(dynamicCG, tokenEdge->getChild()))
        {
            funcGraph = DirectRecursionFunctionBFS(*tokenEdge->getCallEdges().begin());
        }
        else
        {
            funcGraph = SimpleFunctionBFS(*tokenEdge->getCallEdges().begin());
        }
        // now inline the function at each of its entrances
        for (const auto &fe : cs)
        {
            for (const auto &ce : fe->getCallEdges())
            {
#ifdef DEBUG
                ofstream LastGraphPrint("LastGraphPrint.dot");
                auto LastGraph = GenerateHighlightedSubgraph(graph, funcGraph);
                LastGraphPrint << LastGraph << "\n";
                LastGraphPrint.close();
#endif
                // remove the parts of the graph that are unreachable at this particular callsite
                removeUnreachableNodes(funcGraph, ce);
                auto virtEdges = VirtualizeFunctionSubgraph(graph, funcGraph, ce, ce->rets.dynamicRets);
                virtualizedEdges.insert(virtEdges.begin(), virtEdges.end());
            }
        }
        // clean up nodes and edges that have been virtualized
        for (const auto &ve : virtualizedEdges)
        {
            for (const auto &sub : ve->getEdges())
            {
                graph.removeEdge(sub);
            }
        }
        for (const auto &n : graph.nodes())
        {
            auto nodePreds = n->getPredecessors();
            for (const auto &p : nodePreds)
            {
                if( auto i = dynamic_pointer_cast<ImaginaryEdge>(p) )
                {
                    continue;
                }
                else if (!graph.find(p))
                {
                    n->removePredecessor(p);
                }
            }
            auto nodeSuccs = n->getSuccessors();
            for (const auto &s : nodeSuccs)
            {
                if( auto i = dynamic_pointer_cast<ImaginaryEdge>(s) )
                {
                    continue;
                }
                if (!graph.find(s))
                {
                    n->removeSuccessor(s);
                }
            }
        }
        auto graphNodes = graph.getNodes();
        for (const auto &node : graphNodes)
        {
            auto preds = node->getPredecessors();
            auto succs = node->getSuccessors();
            if (preds.empty() && succs.empty())
            {
                graph.removeNode(node);
            }
        }
#ifdef DEBUG
        ofstream LastTransform("LastFunctionInlineTransform.dot");
        auto LastTransformGraph = GenerateDot(graph);
        LastTransform << LastTransformGraph << "\n";
        LastTransform.close();
#endif
        Checks(graph, "FunctionInlineTransform");
    }

#ifdef DEBUG
    ofstream LastTransform("FinalFunctionInlineTransform.dot");
    auto LastTransformGraph = GenerateDot(graph);
    LastTransform << LastTransformGraph << "\n";
    LastTransform.close();
#endif
}

std::vector<shared_ptr<MLCycle>> Cyclebite::Graph::VirtualizeKernels(std::set<shared_ptr<MLCycle>, KCompare> &newKernels, ControlGraph &graph)
{
    vector<shared_ptr<MLCycle>> newPointers;
    for (const auto &kernel : newKernels)
    {
        auto VN = static_pointer_cast<VirtualNode>(kernel);
        auto subgraph = ControlGraph( kernel->getSubgraph(), kernel->getSubgraphEdges(), (*(kernel->getEntrances().begin()))->getWeightedSnk() );
        
#ifdef DEBUG
        auto subgraphString  = GenerateDot(subgraph);
        auto beforeTransform = GenerateDot(graph);
#endif
        VirtualizeSubgraph(graph, VN, subgraph);
        // now balance the probabilities that come out of the node
        uint64_t totalFreq = 0;
        for( const auto& succ : VN->getSuccessors() )
        {
            totalFreq += succ->getFreq();
        }
        for( auto& succ : VN->getSuccessors() )
        {
            if( auto ce = dynamic_pointer_cast<ConditionalEdge>(succ) )
            {
                ce->setWeight(totalFreq);
            }
        }
        newPointers.push_back(kernel);
#ifdef DEBUG
        ofstream LastTransform("LastVirtualizationTransform.dot");
        auto LastTransformGraph = GenerateDot(graph);
        string output = "# Kernel Virtualization\n# Kernel Subgraph\n" + subgraphString + "\n# Old Graph\n" + beforeTransform + "\n# New Graph\n" + LastTransformGraph;
        LastTransform << output << "\n";
        LastTransform.close();
        Checks(graph, "Kernel Virtualization", true);
#endif
    }
    return newPointers;
}


enum Color
{
    White,
    Red,
    Yellow,
    Blue,
    Green
};

const std::shared_ptr<ControlNode> GreenEdgeDFS(const set<shared_ptr<UnconditionalEdge>, GECompare> &greens, ControlGraph &subgraph, const map<shared_ptr<UnconditionalEdge>, Color, GECompare>& colors, const shared_ptr<ControlNode>& source)
{
    set<shared_ptr<ControlNode>, p_GNCompare> subgraphNodes;
    for( const auto& edge : greens )
    {
        subgraphNodes.insert(edge->getWeightedSrc());
        subgraphNodes.insert(edge->getWeightedSnk());
    }

    // we don't want to transform small subgraphs.. that is not what this transform is for
    // therefore, the subgraph must have at least 3 edges in it to be eligible
    if( greens.size() < 3 )
    {
        return nullptr;
    }

    // first condition for a valid graph: graph must be entered through one node and exited through one node
    set<shared_ptr<ControlNode>, p_GNCompare> entranceNodes;
    set<shared_ptr<ControlNode>, p_GNCompare> exitNodes;
    for( const auto& node : subgraphNodes )
    {
        for( const auto& pred : node->getPredecessors() )
        {
            if( colors.find(pred) == colors.end() )
            {
                return nullptr;
            }
            else if( (colors.at(pred) == Color::Red) && (node == source) )
            {
                // we are only interested in entrances to the source node that are red
                entranceNodes.insert(node);
            }
        }
        for( const auto& succ : node->getSuccessors() )
        {
            if( greens.find(succ) == greens.end() )
            {
                exitNodes.insert(node);
            }
        }
    }
    if ((entranceNodes.size() != 1) || (exitNodes.size() != 1))
    {
        return nullptr;
    }
    // second, no nodes within the subgraph are allowed to have 0 preds and 0 succs
    for( const auto& node : subgraphNodes )
    {
        if( node->getPredecessors().empty() || node->getSuccessors().empty() )
        {
            return nullptr;
        }
    }
    // third, only source is allowed to have red predecessors
    for( const auto& node : subgraphNodes )
    {
        for( const auto& pred : node->getPredecessors() )
        {
            if( colors.find(pred) == colors.end() )
            {
                return nullptr;
            }
            else if( (colors.at(pred) == Color::Red) )
            {
                if( node == source )
                {
                    continue;
                }
                else
                {
                    return nullptr;
                }
            }
        }
    }

    subgraph.addNodes(Cyclebite::Graph::NodeConvert(subgraphNodes));
    subgraph.addEdges(Cyclebite::Graph::EdgeConvert(greens));
    return (*exitNodes.begin());
}

const std::shared_ptr<ControlNode> Cyclebite::Graph::FindNewSubgraph(ControlGraph &subgraph, const shared_ptr<ControlNode> &source)
{
    /// This algorithm builds the subgraph starting from source and ending in a sink node that encapsulates a subgraph fit for transforming
    /// Possible transforms that come from this algorithm are
    /// FanInFanOut: subgraph contains a graph that is completely encapsulated within the source and sink node (all edges into and out of the subgraph go through source and sink)

    /// Algorithm: find a subgraph in which all edges are "explained" (an explained edge is one in which both source and sink node are members of the current subgraph)
    /// White: untouched (0) [we know nothing about this edge]
    /// Red: sink node is explained (1) [we are confident edge->getWeightedSnk() is required to get to a valid sink but we know nothing about edge->getWeightedSrc()]
    /// - the only two nodes who are allowed to have red edges as preds are the source and sink nodes
    /// - the source node is for obvious reasons
    /// - to understand the sink node, imagine a valid subgraph whose sink node has a back edge to a loop as predecessor. It is still a valid subgraph, but we won't explore the loop back edge. 
    /// Yellow: source node is explained (1) [we are confident edge->getWeightedSrc() is required to get to a valid sink but we know nothing about edge->getWeightedSnk()]
    /// Blue: currently being evaluated (2) [edge->getWeightedSrc() has been explained and edge->getWeightedSnk() is likely required to get to a valid sink]
    /// Green: fully explained (3) [we know both nodes of this edge are required to get to a valid sink node]
    /// Explained node: all predecessor edges leading into this node are green

    if (source->getSuccessors().empty())
    {
        return nullptr;
    }

    // initialize colored graph
    map<shared_ptr<UnconditionalEdge>, Color, GECompare> colors;
    unsigned int lastGreenSetSize = 0;
    for( const auto& pred : source->getPredecessors() )
    {
        colors[pred] = Color::Red;
    }
    for (const auto &succ : source->getSuccessors())
    {
        colors[succ] = Color::Yellow;
    }

    // iterate until we find a valid subgraph or we eat the max graph size
    while (lastGreenSetSize < MAX_BOTTLENECK_SIZE)
    {
        // first, evaluate yellow edges to build out a potential subgraph further
        set<shared_ptr<UnconditionalEdge>, GECompare> yellowCopy;
        set<shared_ptr<UnconditionalEdge>, GECompare> atLeastRed;
        set<shared_ptr<UnconditionalEdge>, GECompare> atLeastYellow;
        for( const auto& entry : colors )
        {
            if( entry.second == Color::Red )
            {
                atLeastRed.insert(entry.first);
            }
            else if( entry.second == Color::Yellow )
            {
                atLeastRed.insert(entry.first);
                yellowCopy.insert(entry.first);
                atLeastYellow.insert(entry.first);
            }
            else if( entry.second > Color::Yellow )
            {
                atLeastYellow.insert(entry.first);
            }
        }
        for (const auto &ye : yellowCopy)
        {
            // for this yellow edge
            // if all of its sink node's predecessors are at least red, upgrade its successors to yellow
            // if all of its sink node's predecessors are at least yellow, upgrade this edge to blue
            bool allRedOrGreater = true;
            bool allYellowOrGreater = true;
            for( const auto& pred : ye->getWeightedSnk()->getPredecessors() )
            {
                if( atLeastRed.find(pred) == atLeastRed.end() )
                {
                    allRedOrGreater = false;
                    allYellowOrGreater = false;
                    break;
                }
                else if( atLeastYellow.find(pred) == atLeastYellow.end() )
                {
                    allYellowOrGreater = false;
                }
            }
            if( allRedOrGreater )
            {
                colors[ye] = Color::Blue;
            }
            if( allYellowOrGreater )
            {
                // perfect overlap, upgrade this yellow edge to blue
                for (const auto &succ : ye->getWeightedSnk()->getSuccessors())
                {
                    if (colors[succ] == Color::White)
                    {
                        colors[succ] = Color::Yellow;
                    }
                    else if (colors[succ] == Color::Red)
                    {
                        colors[succ] = Color::Yellow;
                    }
                }
            }
            // imagine the case where the sink node has a predecessor that is the back edge to a loop
            // we don't want to take that entire loop, thus we don't explore what's on the other side of the predecessor
            // but we still have a valid subgraph... that will lead into the loop that follows
            // thus we mark this edge red, and all predecessors of a red edge are allowed to be 
            for (const auto &pred : ye->getWeightedSnk()->getPredecessors())
            {
                if (pred == ye)
                {
                    // skip the edge under test
                    continue;
                }
                if (colors[pred] == Color::White)
                {
                    // the snk node of this edge has been touched by the algorithm, but that node also has untouched preds, therefore we know something about the snk of this edge but can't yet explain the src
                    colors[pred] = Color::Red;
                }
            }
        }
        // second, turn eligible blue edges green, and downgrade ineligible blue edges to yellow
        set<shared_ptr<UnconditionalEdge>, GECompare> blueCopy;
        set<shared_ptr<UnconditionalEdge>, GECompare> eligible;
        for( const auto& entry : colors )
        {
            if( entry.second == Color::Blue )
            {
                blueCopy.insert(entry.first);
                eligible.insert(entry.first);
            }
            else if( entry.second > Color::Blue )
            {
                eligible.insert(entry.first);
            }
        }
        for (const auto &be : blueCopy)
        {
            // test the nodes that lie on the other side of the blue edges
            // if all their predecessors are either blue or green, that edge is now green (added to the subgraph)
            // else the edge is downgraded to yellow
            bool allFound = true;
            for( const auto& pred : be->getWeightedSnk()->getPredecessors() )
            {
                if( eligible.find(pred) == eligible.end() )
                {
                    allFound = false;
                    break;
                }
            }
            if (allFound)
            {
                colors[be] = Color::Green;
                // successors of the sink node of this blue edge may become yellow if all preds of that sink node are at least yellow
                bool allYellow = true;
                for( const auto& pred : be->getWeightedSnk()->getPredecessors() )
                {
                    if( colors[pred] < Color::Yellow )
                    {
                        allYellow = false;
                    }
                }
                if( allYellow )
                {
                    for( const auto& succ : be->getWeightedSnk()->getSuccessors() )
                    {
                        colors[succ] = Color::Yellow;
                    }
                }
            }
            else
            {
                // mark the blue edge (be) yellow
                colors[be] = Color::Yellow;
            }
        }

        // third and final, do DFS to find a common sink node among the green edges
        set<shared_ptr<UnconditionalEdge>, GECompare> greenSet;
        for( const auto& entry : colors )
        {
            if( entry.second == Color::Green )
            {
                greenSet.insert(entry.first);
            }
        }
        auto sink = GreenEdgeDFS(greenSet, subgraph, colors, source);
        if (sink)
        {
            // the found subgraph is not allowed to contain cycles
            if (FindCycles(subgraph))
            {
                subgraph.clear();
                return nullptr;
            }
            return sink;
        }
        else
        {
            unsigned int currentGreenSize = 0;
            for( const auto& entry : colors )
            {
                if( entry.second == Color::Green )
                {
                    currentGreenSize++;
                }
            }
            if (lastGreenSetSize >= currentGreenSize )
            {
                // we haven't found any new eligible edges in the new iteration, which means we have found an impasse
                subgraph.clear();
                return nullptr;
            }
            else
            {
                lastGreenSetSize = currentGreenSize;
            }
        }
    }
    return nullptr;
}

void LowFrequencyLoopTransform(ControlGraph& graph)
{
    // John [9/30/2022]
    // be careful allowing lf loops to have multiple entrances/exits
    // it is possible to find a low frequency loop that is located within a partially transformed loop with many entrances and exits

    // since we cannot transform cycles that overlap in the same pass (because nodes and edges can possibly be virtualized) we have to iteratively take away eligible low frequency loops until they are all gone
    auto size = graph.edge_count() - 1;
    while( size < graph.edge_count() )
    {
        set<shared_ptr<MLCycle>, KCompare> newCycles;
        // first, find min paths in the graph
        for (const auto &node : graph.nodes())
        {
            auto nodeIDs = Dijkstras(graph, node->ID(), node->ID());
            if (!nodeIDs.empty())
            {
                // check for cycles within the kernel, if it has more than 1 cycle this kernel will be thrown out
                // to do this, we remove the node that we used to find this cycle from the node set and run FindCycles()
                // removing that node should break the cycle that we already found, so if any other cycles exist, FindCycles will return true
                auto newCycle = make_shared<MLCycle>();
                for (auto id : nodeIDs)
                {
                    auto n = graph.getNode(id);
                    newCycle->addNode(n);
                    for(const auto& pred : n->getPredecessors() )
                    {
                        if( nodeIDs.find(pred->getSrc()->ID()) != nodeIDs.end() )
                        {
                            newCycle->addEdge(pred);
                        }
                    }
                    for( const auto& succ : n->getSuccessors() )
                    {
                        if( nodeIDs.find(succ->getSnk()->ID()) != nodeIDs.end() )
                        {
                            newCycle->addEdge(succ);
                        }
                    }
                }
                // check whether this cycle has only 1 entrance and 1 exit, is completely unique, and is low-frequency
                bool valid = true;
                if( (newCycle->getEntrances().size()) != 1 || (newCycle->getExits().size() != 1) )
                {
                    valid = false;
                }
                // set of kernels that are being kicked out of the newCycles set
                for (const auto &kern : newCycles)
                {
                    if (!kern->Compare(*newCycle).empty())
                    {
                        // if any overlap, this cycle has to wait until a later iteration
                        valid = false;
                        break;
                    }
                }
                if( newCycle->getAnchor() >= MIN_ANCHOR )
                {
                    valid = false;
                }
                if (valid)
                {
                    newCycles.insert(newCycle);
                }
            }
        }

        // transform them
        for( const auto& l : newCycles )
        {
            ControlGraph c(l->getSubgraph(), l->getSubgraphEdges(), (*(l->getEntrances().begin()))->getWeightedSnk());
#ifdef DEBUG
            ofstream LastTransform("LastLowFrequencyLoopTransform.dot");
            string DotString = "# LowFrequencyLoop\n\n# Subgraph\n";
            DotString += GenerateDot(c);
            DotString += "\n# Old Graph\n";
            DotString += GenerateDot(graph);
            DotString += "\n# New Graph\n";
#endif
            auto VN = make_shared<VirtualNode>();
            VirtualizeSubgraph(graph, VN, c);
#ifdef DEBUG
            // normalize exit edge to 1, if necessary
            if( VN->getSuccessors().size() == 1 )
            {
                if( (*(VN->getSuccessors().begin()))->getWeight() < 0.999 )
                {
                    // normalize to 1
                    if( auto ce = dynamic_pointer_cast<ConditionalEdge>(*(VN->getSuccessors().begin())) )
                    {
                        ce->setWeight(ce->getFreq());
                    }
                }
            }
            DotString += GenerateDot(graph);
            LastTransform << DotString << "\n";
            LastTransform.close();
            Checks(graph, "Low Frequency Loop Transform", true);
#endif
        }
        // update new graph size and loop
        size = graph.edge_count();
    }
}

bool KCLTransform(ControlGraph& graph)
{
    // for this algorithm we simply go node-by-node
    // in order for the edges of a node to be transformed
    //  1. the node must have only 1 entrance and 1 exit
    //  2. there must be a discrepancy in the incoming edge frequency and outgoing edge frequency
    // we take the minimum value of the incoming and outgoing edge, that is, f(BC) = min( f(AB), f(BC) )
    bool didChange = false;
    for( const auto& node : graph.getControlNodes() )
    {
        if( (node->getPredecessors().size() == 1) && (node->getSuccessors().size() == 1) )
        {
            auto pred = (*(node->getPredecessors().begin()));
            auto succ = (*(node->getSuccessors().begin()));
            if( pred->getFreq() != succ->getFreq() )
            {
                auto minEdge = succ->getFreq() < pred->getFreq() ? succ : pred;
                auto maxEdge = succ->getFreq() > pred->getFreq() ? succ : pred;
                set<shared_ptr<UnconditionalEdge>, GECompare> oldEdge;
                oldEdge.insert(maxEdge);
                auto VE = make_shared<VirtualEdge>(minEdge->getFreq(), maxEdge->getWeightedSrc(), maxEdge->getWeightedSnk(), oldEdge);
                // if the edge we are replacing has a probability that is not 1, we need to normalize its new frequency to get the same probability again
                // to do this, we take the new frequency and divide it by the old probability, to get us a fraction that will equate to the target probability
                VE->setWeight( (uint64_t)( (float)minEdge->getFreq() / maxEdge->getWeight() ) );
                maxEdge->getWeightedSrc()->removeSuccessor(maxEdge);
                maxEdge->getWeightedSnk()->removePredecessor(maxEdge);
                maxEdge->getWeightedSrc()->addSuccessor(VE);
                maxEdge->getWeightedSnk()->addPredecessor(VE);
                // when the source node successor edge is replaced (maxEdge->getWeightedSrc()->add/removeSucc()...) this can change the outgoing probabilities
                // this code re-normalizes all those edges
                uint64_t totalFreq = 0;
                for( const auto& succ : maxEdge->getWeightedSrc()->getSuccessors() )
                {
                    totalFreq += succ->getFreq();
                }
                for( auto& succ : maxEdge->getWeightedSrc()->getSuccessors() )
                {
                    if( auto ce = dynamic_pointer_cast<ConditionalEdge>(succ) )
                    {
                        ce->setWeight(totalFreq);
                    }
                }
                graph.removeEdge(maxEdge);
                graph.addEdge(VE);
                didChange = true;
            }
        }
    }
    return didChange;
}

void Cyclebite::Graph::ApplyCFGTransforms(ControlGraph &graph, const Cyclebite::Graph::CallGraph &dynamicCG, bool segmentations)
{
    if(!segmentations)
    {
        #ifdef DEBUG
            ofstream debugStream2("MarkovControlGraph.dot");
            auto staticGraph = GenerateDot(graph);
            debugStream2 << staticGraph << "\n";
            debugStream2.close();
        #endif
    }
    // if segmentations is true, sum to one checks are not done
    try
    {
        auto graphSize = graph.size();
        timespec start, end, trivial_start, trivial_end, fifo_start, fifo_end, lfLoop_start, lfLoop_end;
        double totalTime;
        string DotString;
        if (!segmentations)
        {
            // Inline all the shared functions in the graph
            // this must be done before any transforms are applied to the graph (function call edges can be covered up by virtual nodes when they are part of a transform)
            DotString = "# SharedFunction\n\n# Subgraph\n";
            DotString += "\n# Old Graph\n";
            DotString += GenerateDot(graph);
            while (clock_gettime(CLOCK_MONOTONIC, &start))
            {
            }
            VirtualizeSharedFunctions(graph, dynamicCG);
            // after virtualizing functions we attempt to balance out any discrepancies in the frequency flow of the graph, through the Kirkhoff's Current Law transform (flow out of a node must equal flow into that node)
            KCLTransform(graph); 
            while (clock_gettime(CLOCK_MONOTONIC, &end))
            {
            }
            totalTime = CalculateTime(&start, &end);
            spdlog::info("SHAREDFUNCTIONTRANSFORMTIME: " + to_string(totalTime));
        }

        while (clock_gettime(CLOCK_MONOTONIC, &start))
        {
        }
#ifdef DEBUG
        if (graphSize != graph.size())
        {
            DotString += "\n# New Graph\n";
            ofstream LastTransform("LastTransform.dot");
            DotString += GenerateDot(graph);
            LastTransform << DotString << "\n";
            LastTransform.close();
            if (!segmentations)
            {
                SumToOne(graph.getNodes());
            }
            graphSize = graph.size();
        }
#endif

        // do all trivial and branch->select transforms first (ie fixed depth transforms), this will reduce the complexity of the more complicated transforms
        set<shared_ptr<ControlNode>, p_GNCompare> coveredTrivials;
        deque<shared_ptr<ControlNode>> Q;
        Q.push_front(graph.getFirstNode());
        while(clock_gettime(CLOCK_MONOTONIC, &trivial_start)) {}
        while (!Q.empty())
        {
            if (!graph.find(Q.front()) || (coveredTrivials.find(Q.front()) != coveredTrivials.end()))
            {
                coveredTrivials.insert(Q.front());
                Q.pop_front();
                continue;
            }

            auto sub = TrivialTransforms(Q.front());
            if (!sub.empty())
            {
#ifdef DEBUG
                ofstream LastTransform("LastTransform.dot");
                string DotString = "# Trivial Transform\n\n# Subgraph\n";
                DotString += GenerateDot(sub);
                DotString += "\n# Old Graph\n";
                DotString += GenerateDot(graph);
                DotString += "\n# New Graph\n";
#endif
                auto VN = make_shared<VirtualNode>();
                VirtualizeSubgraph(graph, VN, sub);
#ifdef DEBUG
                DotString += GenerateDot(graph);
                LastTransform << DotString << "\n";
                LastTransform.close();
                if (!segmentations)
                {
                    SumToOne(graph.getNodes());
                }
#endif
                coveredTrivials.insert(VN->getSubgraph().begin(), VN->getSubgraph().end());
                Q.pop_front();
                Q.push_front(VN);
                continue;
            }
            // Next transform, find conditional branches and turn them into select statements
            // In other words, find subgraphs of nodes that have a common entrance and exit, flow from one end to the other, and combine them into a single node
            sub = BranchToSelectTransforms(graph, Q.front());
            if (!sub.empty())
            {
#ifdef DEBUG
                ofstream LastTransform("LastTransform.dot");
                string DotString = "# BranchToSelect\n\n# Subgraph\n";
                DotString += GenerateDot(sub);
                DotString += "\n# Old Graph\n";
                DotString += GenerateDot(graph);
                DotString += "\n# New Graph\n";
#endif
                auto VN = make_shared<VirtualNode>();
                VirtualizeSubgraph(graph, VN, sub);
#ifdef DEBUG
                DotString += GenerateDot(graph);
                LastTransform << DotString << "\n";
                LastTransform.close();
                if (!segmentations)
                {
                    SumToOne(graph.getNodes());
                }
#endif
                coveredTrivials.insert(VN->getSubgraph().begin(), VN->getSubgraph().end());
                Q.pop_front();
                Q.push_front(VN);
                continue;
            }
            coveredTrivials.insert(Q.front());
            for (const auto &succ : Q.front()->getSuccessors())
            {
                if (coveredTrivials.find(succ->getWeightedSnk()) == coveredTrivials.end())
                {
                    Q.push_back(succ->getWeightedSnk());
                }
            }
            Q.pop_front();
        }
        while( clock_gettime(CLOCK_MONOTONIC, &trivial_end) ){}
        totalTime = CalculateTime(&trivial_start, &trivial_end);
        spdlog::info("CFGSIMPLETRANSFORMTIME: " + to_string(totalTime));

        // this is a global while loop that allows all transforms to transform the graph with each pass, each time possibly opening new opportunities for transform in the next iteration
        // the algorithm terminates when an iteration results in no changes to the size of the graph (where size of the graph is nodes+edges)
        // we start one less than the graph size to allow the algorithm to start
        graphSize = graph.size() - 1;
        while(clock_gettime(CLOCK_MONOTONIC, &fifo_start)) {}
        while (graphSize < graph.size())
        {
            coveredTrivials.clear();
            Q.push_front(graph.getFirstNode());
            while (!Q.empty())
            {
                if (!graph.find(Q.front()) || (coveredTrivials.find(Q.front()) != coveredTrivials.end()))
                {
                    coveredTrivials.insert(Q.front());
                    Q.pop_front();
                    continue;
                }
                // combine all trivial node merges
                auto sub = TrivialTransforms(Q.front());
                if (!sub.empty())
                {
#ifdef DEBUG
                    ofstream LastTransform("LastTransform.dot");
                    string DotString = "# Trivial Transform\n\n# Subgraph\n";
                    DotString += GenerateDot(sub);
                    DotString += "\n# Old Graph\n";
                    DotString += GenerateDot(graph);
                    DotString += "\n# New Graph\n";
#endif
                    auto VN = make_shared<VirtualNode>();
                    VirtualizeSubgraph(graph, VN, sub);
#ifdef DEBUG
                    DotString += GenerateDot(graph);
                    LastTransform << DotString << "\n";
                    LastTransform.close();
                    if (!segmentations)
                    {
                        SumToOne(graph.getNodes());
                    }
#endif
                    coveredTrivials.insert(VN->getSubgraph().begin(), VN->getSubgraph().end());
                    Q.pop_front();
                    Q.push_front(VN);
                    continue;
                }
                // Next transform, find conditional branches and turn them into select statements
                // In other words, find subgraphs of nodes that have a common entrance and exit, flow from one end to the other, and combine them into a single node
                sub = BranchToSelectTransforms(graph, Q.front());
                if (!sub.empty())
                {
#ifdef DEBUG
                    ofstream LastTransform("LastTransform.dot");
                    string DotString = "# BranchToSelect\n\n# Subgraph\n";
                    DotString += GenerateDot(sub);
                    DotString += "\n# Old Graph\n";
                    DotString += GenerateDot(graph);
                    DotString += "\n# New Graph\n";
#endif
                    auto VN = make_shared<VirtualNode>();
                    VirtualizeSubgraph(graph, VN, sub);
#ifdef DEBUG
                    DotString += GenerateDot(graph);
                    LastTransform << DotString << "\n";
                    LastTransform.close();
                    if (!segmentations)
                    {
                        SumToOne(graph.getNodes());
                    }
#endif
                    coveredTrivials.insert(VN->getSubgraph().begin(), VN->getSubgraph().end());
                    Q.pop_front();
                    Q.push_front(VN);
                    continue;
                }
                ControlGraph newSub;
                auto sink = FindNewSubgraph(newSub, Q.front());
                if (!newSub.empty())
                {
#ifdef DEBUG
                    ofstream LastTransform("LastTransform.dot");
                    string DotString = "# Complex Transform\n\n# Subgraph\n";
                    DotString += GenerateDot(newSub);
                    DotString += "\n# Old Graph\n";
                    DotString += GenerateDot(graph);
                    DotString += "\n# New Graph\n";
#endif
                    auto VN = make_shared<VirtualNode>();
                    VirtualizeSubgraph(graph, VN, newSub);
#ifdef DEBUG
                    DotString += GenerateDot(graph);
                    LastTransform << DotString << "\n";
                    LastTransform.close();
                    if (!segmentations)
                    {
                        SumToOne(graph.getNodes());
                    }
#endif
                    coveredTrivials.insert(VN->getSubgraph().begin(), VN->getSubgraph().end());
                    Q.pop_front();
                    Q.push_front(VN);
                    continue;
                }
                // Transform bottlenecks to avoid multiple entrance/multiple exit kernels
                if (FanInFanOutTransform(newSub, Q.front(), sink))
                {
#ifdef DEBUG
                    ofstream LastTransform("LastTransform.dot");
                    string DotString = "# FanInFanOut\n\n# Subgraph\n";
                    DotString += GenerateDot(sub);
                    DotString += "\n# Old Graph\n";
                    DotString += GenerateDot(graph);
                    DotString += "\n# New Graph\n";
#endif
                    auto VN = make_shared<VirtualNode>();
                    VirtualizeSubgraph(graph, VN, newSub);
#ifdef DEBUG
                    DotString += GenerateDot(graph);
                    LastTransform << DotString << "\n";
                    LastTransform.close();
                    if (!segmentations)
                    {
                        SumToOne(graph.getNodes());
                    }
#endif
                    coveredTrivials.insert(VN->getSubgraph().begin(), VN->getSubgraph().end());
                    Q.pop_front();
                    Q.push_front(VN);
                    continue;
                }
                coveredTrivials.insert(Q.front());
                for (const auto &succ : Q.front()->getSuccessors())
                {
                    if (coveredTrivials.find(succ->getWeightedSnk()) == coveredTrivials.end())
                    {
                        Q.push_back(succ->getWeightedSnk());
                    }
                }
                Q.pop_front();
            } // !Q.empty

            while( clock_gettime(CLOCK_MONOTONIC, &fifo_end) ){}
            totalTime = CalculateTime(&fifo_start, &fifo_end);
            spdlog::info("CFGCOMPLEXTRANSFORMTIME: " + to_string(totalTime));

            // get rid of low-frequency loops before kernel analysis
            while( clock_gettime(CLOCK_MONOTONIC, &lfLoop_start) ){}
            LowFrequencyLoopTransform(graph);
            while( clock_gettime(CLOCK_MONOTONIC, &lfLoop_end) ) {}
            totalTime = CalculateTime(&lfLoop_start, &lfLoop_end);
            spdlog::info("LOWFREQUENCYLOOPTRANFORMTIME: " + to_string(totalTime));        
            
            // transform edge frequencies
            auto didChange = KCLTransform(graph);
            if( didChange )
            {
                // guarantees we will do another iteration
                graphSize = graph.size() - 1;
            }
            else
            {
                // will only allow another iteration if the other transforms made changes
                graphSize = graph.size();
            }
        } // while( graphSize != graph.size() )

        while (clock_gettime(CLOCK_MONOTONIC, &end))
        {
        }
        totalTime = CalculateTime(&start, &end);
        spdlog::info("CFGTRANSFORMTIME: " + to_string(totalTime));
    }
    catch (CyclebiteException &e)
    {
        spdlog::critical(e.what());
        exit(EXIT_FAILURE);
    }
#ifdef DEBUG
    if( !segmentations )
    {
        spdlog::info("Transformed Graph:");
        ofstream debugStream3("simplifiedMarkovControlGraph.dot");
        auto transformedStaticGraph = GenerateDot(graph);
        debugStream3 << transformedStaticGraph << "\n";
        debugStream3.close();
    }
#endif
}

void reverse_cycle_transform(ControlGraph& graph, const set<shared_ptr<MLCycle>, p_GNCompare>& toRemove)
{
    auto newGraph = graph;
    // transform the graph until all parent-most-level mlcycles are exposed
    for( const auto& ml : toRemove)
    {
        shared_ptr<MLCycle> foundML = nullptr;
        vector<std::shared_ptr<GraphNode>> tmpNodes(newGraph.nodes().begin(), newGraph.nodes().end());
        for (auto node : tmpNodes)
        {
            if( node == ml )
            {
                foundML = static_pointer_cast<MLCycle>(node);
            }
            else if( dynamic_pointer_cast<MLCycle>(node) == nullptr )
            {
                if (auto VN = dynamic_pointer_cast<VirtualNode>(node) )
                {
                    deque<shared_ptr<VirtualNode>> Q;
                    set<shared_ptr<VirtualNode>> covered;
                    Q.push_front(VN);
                    while( !Q.empty() )
                    {
                        for( const auto& node : Q.front()->getSubgraph() )
                        {
                            if( auto mlc = dynamic_pointer_cast<MLCycle>(node) )
                            {
                                if( mlc == ml )
                                {
                                    foundML = mlc;
                                    break;
                                }
                            }
                            else if( auto vn = dynamic_pointer_cast<VirtualNode>(node) )
                            {
                                Q.push_back(vn);
                            }
                        }
                        if( foundML )
                        {
                            break;
                        }
                        Q.pop_front();
                    }
                }
            }
            if( foundML )
            {
                // two steps:
                // 1. get rid of virtual entrance/exit edges and put their underlyings back into the edges set
                // 2. get rid of virtual node and puts its subgraph back into the nodes set
                for (auto ent : foundML->getPredecessors())
                {
                    if (auto VE = dynamic_pointer_cast<VirtualEdge>(ent))
                    {
                        newGraph.removeEdge(VE);
                        VE->getSrc()->removeSuccessor(VE);
                        VE->getSrc()->addSuccessor(*VE->getEdges().begin());
                        newGraph.addEdge(*VE->getEdges().begin());
                    }
                    else
                    {
                        // do nothing, the edge has no underlyings and already points to the correct nodes
                    }
                }
                for (auto ex : foundML->getSuccessors())
                {
                    if (auto VE = dynamic_pointer_cast<VirtualEdge>(ex))
                    {
                        newGraph.addEdges(EdgeConvert(VE->getEdges()));
                        newGraph.removeEdge(VE);
                        VE->getSnk()->removePredecessor(VE);
                        VE->getSnk()->addPredecessor(*VE->getEdges().begin());
                    }
                    else
                    {
                        // do nothing, the edge has no underlyings and already points to the correct nodes
                    }
                }
                newGraph.addNodes(Cyclebite::Graph::NodeConvert(foundML->getSubgraph()));
                newGraph.addEdges(Cyclebite::Graph::EdgeConvert(foundML->getSubgraphEdges()));
                newGraph.removeNode(foundML);
            }
        }
    }
    graph = newGraph;
}

set<shared_ptr<MLCycle>, KCompare> Cyclebite::Graph::FindMLCycles(ControlGraph &graph, const Cyclebite::Graph::CallGraph &dynamicCG, bool applyTransforms)
{
    // master set of kernels, holds all valid kernels parsed from the CFG
    // each kernel in here is represented in the call graph by a virtual kernel node
    set<shared_ptr<MLCycle>, KCompare> kernels;
    int64_t kernelCount = (int64_t)(kernels.size() - 1); // -1 to start the iteration
    int iterator = 0;
    while (kernelCount < (int64_t)kernels.size() )
    {
        kernelCount = (int64_t)kernels.size();
        // holds kernels parsed from this iteration
        set<shared_ptr<MLCycle>, KCompare> newKernels;
        // first, find min paths in the graph
        for (const auto &node : graph.nodes())
        {
            auto nodeIDs = Dijkstras(graph, node->ID(), node->ID());
            if (!nodeIDs.empty())
            {
                // turn the blockIDs of the cycle into nodes and capture the edges of the cycle
                auto newKernel = make_shared<MLCycle>();
                for (auto id : nodeIDs)
                {
                    auto n = graph.getNode(id);
                    newKernel->addNode(n);
                    for(const auto& pred : n->getPredecessors() )
                    {
                        if( nodeIDs.find(pred->getSrc()->ID()) != nodeIDs.end() )
                        {
                            newKernel->addEdge(pred);
                        }
                    }
                    for( const auto& succ : n->getSuccessors() )
                    {
                        if( nodeIDs.find(succ->getSnk()->ID()) != nodeIDs.end() )
                        {
                            newKernel->addEdge(succ);
                        }
                    }
                }
                // check whether this kernel is valid
                // a kernel is invalid if
                //  1. it contains another cycle (because all low-frequency cycles have been done away with by the LFLTransform)
                //  2. kernel that has already been found
                //  3. edge frequencies do not meet a threshold (see MLCYCLE_ANCHOR_THRESHOLD)
                //  4. kernel must have at least one entrance and one exit
                bool valid = true;
                // 1. check for cycles within the kernel, if it has more than 1 cycle this kernel will be thrown out
                // to check for cycles, we remove the node that we used to find this cycle from the node set and run FindCycles()
                // removing that node should break the cycle that we already found, so if any other cycles exist, FindCycles will return true
                // corner case: the node we remove cycles on itself, so don't forget to check the node we remove
                ControlGraph cycle(newKernel->getSubgraph(), newKernel->getSubgraphEdges(), *newKernel->getSubgraph().begin());
                cycle.removeNode(static_pointer_cast<ControlNode>(node));
                ControlGraph lone;
                lone.addNode(static_pointer_cast<ControlNode>(node));
                lone.addEdges(node->getSuccessors());
                if( FindCycles(cycle) || (cycle.getNodes().size() && FindCycles(lone)) )
                {
                    valid = false;
                }
                // 2. this kernel has not yet been found
                for (const auto &kern : newKernels)
                {
                    auto shared = kern->Compare(*newKernel);
                    if (shared.size() == kern->getSubgraph().size())
                    {
                        // if perfect overlap, this kernel has already been found
                        valid = false;
                    }
                }
                // 3. edge frequencies meet the threshold
                if( newKernel->getAnchor() < MIN_ANCHOR )
                {
                    valid = false;
                }
                // 4. kernel must have at least one entrance and one exit
                if( newKernel->getEntrances().empty() || newKernel->getExits().empty() )
                {
                    valid = false;
                }
                if (valid)
                {
                    newKernels.insert(newKernel);
                }
            }
        }
        int minScore = INT_MAX;
        shared_ptr<MLCycle> min_score_winner = nullptr;
        for (auto &kern : newKernels)
        {
            if (kern->EnExScore() < minScore)
            {
                minScore = kern->EnExScore();
                min_score_winner = kern;
            }
        }
        // next set of tests ensures we structure the loops in the "correct order"
        // "correct order": structure loops in order of non-increasing entrance+exit count, then non-increasing path probability - thus the loops with fewest entrances/exits - highest-path-probability get structured first
        //  - this ensures we structure a loop hierarchy from child-most to parent-most (to our knowledge, counter-examples could be possible) 
        set<shared_ptr<MLCycle>, KCompare> toRemove;
        for (auto kern : newKernels)
        {
            if (toRemove.find(kern) != toRemove.end())
            {
                continue;
            }
            else if ( (kern->EnExScore() > minScore) && (toRemove.find(min_score_winner) == toRemove.end()) )
            {
                toRemove.insert(kern);
                continue;
            }
            else
            {
                // you are equal to the score, so compare to others
                for (auto compare : newKernels)
                {
                    if ((kern == compare) || (toRemove.find(compare) != toRemove.end()))
                    {
                        continue;
                    }
                    if (!kern->Compare(*compare).empty())
                    {
                        if (kern->PathProbability() > compare->PathProbability())
                        {
                            toRemove.insert(compare);
                        }
                        else if( (kern->PathProbability() - compare->PathProbability()) < 0.001 )
                        {
                            // this is a floating point tie
                            // on ties we keep the kern and throw out the compare
                            // this is an arbitrary decision, and keeps us from the case where (toward the end of the segmentation iterations) we don't take any loops even though there are some left
                            toRemove.insert(compare);
                        }
                        else
                        {
                            toRemove.insert(kern);
                        }
                    }
                }
            }
        }
        // corner case: each of the remaining loops manages to defeat the other eligible loops because of awkward control structure (example: MiBench/office/stringsearch/search_large)
        // in this case we pick the loop with minimum exit probability
        if( toRemove.size() == newKernels.size() )
        {
            toRemove.clear();
            float min_path_prob = __FLT_MAX__;
            shared_ptr<MLCycle> winner = nullptr;
            for( const auto& kern : newKernels )
            {
                if( kern->PathProbability() < min_path_prob )
                {
                    min_path_prob = kern->PathProbability();
                    if( winner )
                    {
                        toRemove.insert(winner);
                    }
                    winner = kern;
                }
                else
                {
                    toRemove.insert(winner);
                }
            }
        }
        for (auto r : toRemove)
        {
            newKernels.erase(r);
        }
        auto newPointers = VirtualizeKernels(newKernels, graph);
        if (FindCycles(graph) && applyTransforms)
        {
            ApplyCFGTransforms(graph, dynamicCG, true);
        }
        for (const auto &p : newPointers)
        {
            kernels.insert(p);
        }

#ifdef DEBUG
        spdlog::info("Transformed Graph after " + to_string(iterator) + " iterations:");
        // PrintGraph(graph.nodes);
        ofstream debugStream2("TransformedMarkovControlGraph_" + to_string(iterator) + ".dot");
        auto transformedStaticGraph = GenerateDot(graph);
        debugStream2 << transformedStaticGraph << "\n";
        debugStream2.close();
#endif
        iterator++;
    }

    // now that we have localized all cycles, find the cycles that are incorrectly grouping tasks into hierarchies that don't make sense
    // this can happen when a program is feeding a pipeline of tasks piece-meal with a while loop
    // there are 4 rules here
    // 1. Only outer-most tasks are eligible for revoking
    // 2. At least 2 inner tasks must be present in the revoked cycle
    // 3. Child-tasks of a revoked cycle must be hierarchies, not standalone cycles
    // Algorithm: Progressively revoke eligible tasks until a no revokable task is present
    set<shared_ptr<MLCycle>, p_GNCompare> toRemove;
    toRemove.insert(*kernels.begin());
    while( !toRemove.empty() )
    {
        toRemove.clear();
        // rule 1: only outer-most tasks are eligible
        set<shared_ptr<MLCycle>, p_GNCompare> topLevelTasks;
        for( const auto& t : kernels )
        {
            if( t->getParentKernels().empty() )
            {
                topLevelTasks.insert(t);
            }
        }
        // rule 2: task must have at least 2 children
        set<shared_ptr<MLCycle>, p_GNCompare> atLeast2;
        for( const auto& t : topLevelTasks )
        {
            if( t->getChildKernels().size() > 1 )
            {
                atLeast2.insert(t);
            }
        }
        // rule 3: child tasks must be hierarchies, not standalone tasks
        set<shared_ptr<MLCycle>, p_GNCompare> childHierarchies;
        for( const auto& t : atLeast2 )
        {
            bool allHierarchies = true;
            if( t->getChildKernels().size() < MIN_CHILD_KERNEL_EXCEPTION )
            {                
                for( const auto& c : t->getChildKernels() )
                {
                    if( c->getChildKernels().empty() )
                    {
                        allHierarchies = false;
                        break;
                    }
                }
            }
            if(allHierarchies)
            {
                childHierarchies.insert(t);
            }
        }
        // whoever is left is eligible for removal
        toRemove.insert(childHierarchies.begin(), childHierarchies.end());
        reverse_cycle_transform(graph, toRemove);
        for( const auto& r : toRemove )
        {
            kernels.erase(r);
            for( const auto& c : r->getChildKernels() )
            {
                c->removeParentKernel(r);
            }
        }
    }

#ifdef DEBUG
        spdlog::info("Transformed Graph after " + to_string(iterator) + " iterations:");
        // PrintGraph(graph.nodes);
        ofstream debugStream2("FinalTransformedGraph.dot");
        auto transformedStaticGraph = GenerateDot(graph);
        debugStream2 << transformedStaticGraph << "\n";
        debugStream2.close();
#endif
    return kernels;
}

void Cyclebite::Graph::FindAllRecursiveFunctions(const llvm::CallGraph &CG, const Graph &graph, const std::map<int64_t, const llvm::BasicBlock *> &IDToBlock)
{
    uint32_t CGsize = 0;
    uint32_t totalFunctions = 0;
    uint32_t totalLiveFunctions = 0;
    uint32_t totalFunctionPointers = 0;
    uint32_t IDR = 0;
    uint32_t DR = 0;
    for (auto it = CG.begin(); it != CG.end(); it++)
    {
        CGsize++;
        if ((it->second->getNumReferences() > 0) || (it->second->size() > 0))
        {
            totalFunctions++;
        }
        else
        {
            continue;
        }
        if (it->second->getFunction())
        {
            for (auto fi = it->second->getFunction()->begin(); fi != it->second->getFunction()->end(); fi++)
            {
                auto node = BlockToNode(graph, IDToBlock.at(Cyclebite::Util::GetBlockID(llvm::cast<llvm::BasicBlock>(fi))), NIDMap);
                if (node != nullptr)
                {
                    totalLiveFunctions++;
                    break;
                }
            }
        }
        else
        {
            continue;
        }

        if (hasIndirectRecursion(it->second.get()))
        {
            IDR++;
        }
        else if (hasDirectRecursion(it->second.get()))
        {
            DR++;
        }
    }
    // find the number of function pointers there are by using the external calling node (which points to functions called by function pointers and external "empty" functions)
    for( auto ci = CG.getExternalCallingNode()->begin(); ci != CG.getExternalCallingNode()->end(); ci++ )
    {
        if( ci->second->getFunction() )
        {
            if( (string(ci->second->getFunction()->getName()) != "main") && (!ci->second->getFunction()->empty()) )
            {
                totalFunctionPointers++;
            }
        }
    }
    spdlog::info("CALLGRAPH SIZE: " + to_string(CGsize));
    spdlog::info("TOTAL FUNCTIONS: " + to_string(totalFunctions));
    spdlog::info("TOTAL LIVE FUNCTIONS: " + to_string(totalLiveFunctions));
    spdlog::info("TOTAL FUNCTION POINTERS: " + to_string(totalFunctionPointers));
    spdlog::info("INDIRECT RECURSION FUNCTIONS: " + to_string(IDR));
    spdlog::info("DIRECT RECURSION FUNCTIONS: " + to_string(DR));
}

void Cyclebite::Graph::FindAllRecursiveFunctions(const Cyclebite::Graph::CallGraph &CG, const Graph &graph, const std::map<int64_t, const llvm::BasicBlock *> &IDToBlock)
{
    uint32_t CGsize = 0;
    uint32_t totalFunctions = 0;
    uint32_t totalLiveFunctions = 0;
    uint32_t IDR = 0;
    uint32_t DR = 0;
    for (const auto &node : CG.getCallNodes())
    {
        CGsize++;
        totalFunctions++;
        for (auto fi = node->getFunction()->begin(); fi != node->getFunction()->end(); fi++)
        {
            auto node = BlockToNode(graph, IDToBlock.at(Cyclebite::Util::GetBlockID(llvm::cast<llvm::BasicBlock>(fi))), NIDMap);
            if (node != nullptr)
            {
                totalLiveFunctions++;
                break;
            }
        }
        if (hasIndirectRecursion(CG, node))
        {
            IDR++;
        }
        else if (hasDirectRecursion(CG, node))
        {
            DR++;
        }
    }
    spdlog::info("CALLGRAPH SIZE: " + to_string(CGsize));
    spdlog::info("TOTAL FUNCTIONS: " + to_string(totalFunctions));
    spdlog::info("TOTAL LIVE FUNCTIONS: " + to_string(totalLiveFunctions));
    spdlog::info("INDIRECT RECURSION FUNCTIONS: " + to_string(IDR));
    spdlog::info("DIRECT RECURSION FUNCTIONS: " + to_string(DR));
}

// John: we have been moving from SCCs to cycles.. Are we actually doing that?
// Is a cycle strictly a cycle even though it is composed of virtual nodes, or is it a strongly connected component?

// Ben: I believe we are just finding cycles... transforms are just simplifying cycle subgraphs, we don't include any tails or heads to the cycles... so there shouldn't be any noise to the cycles
// Ben: it is possible that we are eating dangles but it is not intentional
// John: the entrance block and exit block to and from main need to have preds/succs called "root"/"tail", and these nodes are explicitly non-transformable

// John: how many unexpected dangles do we end up with? (evaluation of profiler)

// John: does my dynamic call graph significantly disagree with the static one? Is it possible to call into a function from a BB that cannot actually do that according to the static code?

// John: one of the positions of Rick's work was that... do we actually have to find SCCs or do we just find cycles? we don't know...
// But now we are saying... no, cycles are good enough. we can write down the possible structures within a C program, and finding the cycles is good enough to structure

/* Moving this function to the new edge classes is hard because it is destroying edges... the work to be done is to build new edges as the algorithm progresses
std::set<std::shared_ptr<ControlNode> , p_GNCompare> Cyclebite::Graph::ReduceMO(Graph& graph, int inputOrder, int desiredOrder)
{
    // first step, transform the input nodes into markov order 1 nodes
    // Markov Order Reduction Algorithm:
    // for (inputMarkovOrder - desiredMarkovOrder) iterations
    //   for each node in the prior graph
    //    if this node is not in the skip list
    //      find all nodes that have the same state (markovOrder-1) states ago (eg if 0|-1 is the current node, then 0|-2 and 0|-3 need to be grouped with it; if 0|-1,-2 is the current node, then 0|-1,-3 should be grouped, but not 0|-2,-3)
    //      combine all preds and successors together to form the node at markovOrder-1
    //      add all grouped nodes together into the skip set
    set<std::shared_ptr<ControlNode> , p_GNCompare> previousGraph = graph.nodes;
    // holds new nodes for the markovOrder-1 graph
    set<std::shared_ptr<ControlNode> , p_GNCompare> newGraph;
    if (inputOrder == desiredOrder)
    {
        // copy all nodes into the return graph
        for (const auto node : graph.nodes)
        {
            auto newNode = new ControlNode(*node);
            newGraph.insert(newNode);
        }
        return newGraph;
    }
    int currentOrder = inputOrder;
    while (currentOrder > desiredOrder)
    {
        newGraph.clear();
        // maps an NID from the old graph into the new one
        map<uint64_t, uint64_t> IDMap;
        // nodes that have already been covered
        set<std::shared_ptr<ControlNode> , p_GNCompare> done;
        // sum along the columns of the nodes until we have transformed the transition table one to (markovOrder - 1)
        for (const auto &n : previousGraph)
        {
            if (done.find(n) == done.end())
            {
                // find all that have the same markovOrder-1 state
                vector<std::shared_ptr<ControlNode> > likeNodes;
                for (const auto &otherNode : previousGraph)
                {
                    if (*next(otherNode->originalBlocks.begin()) == *next(n->originalBlocks.begin()))
                    {
                        likeNodes.push_back(otherNode);
                    }
                }
                // make a new node if necessary, else graph the existing replacement (if this node is a neighbor of a node that has already been evaluated, a replacement node already exists for it)
                std::shared_ptr<ControlNode> newNode;
                if (IDMap.find(n->ID()) == IDMap.end())
                {
                    newNode = new ControlNode();
                }
                else
                {
                    newNode = *newGraph.find(IDMap[n->ID()]);
                }
                for (auto og = next(n->originalBlocks.begin()); og != n->originalBlocks.end(); og++)
                {
                    newNode->originalBlocks.push_back(*og);
                    newNode->blocks.insert(*og);
                }
                newGraph.insert(newNode);
                IDMap[n->ID()] = newNode->ID();
                done.insert(n);

                for (const auto &oldNode : likeNodes)
                {
                    // each node gets merged into the new node
                    IDMap[oldNode->ID()] = newNode->ID();
                    done.insert(oldNode);
                    for (const auto &nei : oldNode->getSuccessors())
                    {
                        // make the neighbor node if it doesn't exist yet
                        if (IDMap.find(nei->getWeightedSnk()->ID()) == IDMap.end())
                        {
                            // we have to find out if there is a like node for this neighbor
                            auto oldNeighbor = *previousGraph.find(nei->getWeightedSnk()->ID());
                            bool match = false;
                            for (const auto &likeNeighbor : previousGraph)
                            {
                                if (*next(likeNeighbor->originalBlocks.begin()) == *next(oldNeighbor->originalBlocks.begin()))
                                {
                                    if (IDMap.find(likeNeighbor->ID()) != IDMap.end())
                                    {
                                        IDMap[nei->getWeightedSnk()->ID()] = IDMap[likeNeighbor->ID()];
                                        //done.insert(oldNeighbor);
                                        match = true;
                                        break;
                                    }
                                }
                            }
                            if (!match)
                            {
                                auto newNeighbor = new ControlNode();
                                newGraph.insert(newNeighbor);
                                IDMap[nei->getWeightedSnk()->ID()] = newNeighbor->ID();
                            }
                        }
                        // if this neighbor is already in the neighbor map, add its frequency counts together
                        if (newNode->getSuccessors().find(IDMap[nei->getWeightedSnk()->ID()]) == newNode->getSuccessors().end())
                        {
                            newNode->getSuccessors()[IDMap[nei->getWeightedSnk()->ID()]] = nei.second;
                        }
                        // else add the neighbor
                        else
                        {
                            newNode->getSuccessors()[IDMap[nei->getWeightedSnk()->ID()]].first += nei->getFreq();
                        }
                    }
                    for (const auto &pred : oldNode->getPredecessors())
                    {
                        // do the same thing for the predecessors now
                        if (IDMap.find(pred->getWeightedSrc()->ID()) == IDMap.end())
                        {
                            // we have to find out if there is a like node for this predecessor
                            auto oldPredecessor = *previousGraph.find(pred);
                            bool match = false;
                            for (const auto &likePredecessor : previousGraph)
                            {
                                if (*next(likePredecessor->originalBlocks.begin()) == *next(oldPredecessor->originalBlocks.begin()))
                                {
                                    if (IDMap.find(likePredecessor->ID()) != IDMap.end())
                                    {
                                        IDMap[pred->getWeightedSrc()->ID()] = IDMap[likePredecessor->ID()];
                                        match = true;
                                        //done.insert(oldPredecessor);
                                        break;
                                    }
                                }
                            }
                            if (!match)
                            {
                                auto newPred = new ControlNode();
                                newGraph.insert(newPred);
                                IDMap[pred->getWeightedSrc()->ID()] = newPred->ID();
                            }
                        }
                        newNode->getPredecessors().insert(IDMap[pred->getWeightedSrc()->ID()]);
                    }
                }
            }
        }
        // don't free the input graph, but free any intermediate graphs
        if (currentOrder < inputOrder)
        {
            // we aren't on the first iteration, so free the previous iteration (because it was intermediate)
            for (const auto &node : previousGraph)
            {
                delete node;
            }
        }
        previousGraph = newGraph;
        currentOrder--;
    }
    return newGraph;
}
*/