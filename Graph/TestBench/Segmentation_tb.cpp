// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
#include "Util/Exceptions.h"
#include "ControlGraph.h"
#include "Dijkstra.h"
#include "IO.h"
#include "Util/IO.h"
#include "VirtualEdge.h"
#include "MLCycle.h"
#include "Transforms.h"
#include "CallGraph.h"
#include <fstream>
#include <iostream>
#include <spdlog/spdlog.h>
#include <deque>

#ifndef DEBUG
#define DEBUG 1
#endif

/**
 * Implements elementary test cases for the four transforms used in the Cyclebite program segmentation algorithm
 * 1. Serial merge: merge serial chains of nodes into the source node
 * 2. Branch->Select: merge subgraphs of nodes in which all nodes between a source and sink node that have the source node as their only predecessor and the sink node as their only successor
 * 3. Fanin-Fanout: merge subgraphs of nodes in which the only entrance to the subgraph is the source node and the only exit from the subgraph is the sink node
 * 4. MergeFork: merge nodes that only have a source and sink node as predecessor and successor (respectively), but the source and sink nodes are allowed to have edges to/from other nodes
 */

/**
 * Some (possibly) helpful tips
 * 1. p_GNCompare sorts the nodes in NID order, which dictates the order of evaluation when we apply transforms to the graph. 
 *    - this means that order of definition matters
 *    - considering that the graph algorithms depend on the order they see things (BFS, DFS), this means the tests need to test the same graph in different ordering
 * 
 */

using namespace Cyclebite::Graph;
using namespace std;
using namespace llvm;

// this block map is a patch to make the unit tests work with the existing check]
// the existing trivial transform has a rule that no function boundary can be crossed in the transform
// this map is used to check that
// thus we just populate the thing with one simple basic block such that the check is not exercised (yet)
LLVMContext fakeContext;
BasicBlock *simple = BasicBlock::Create(fakeContext);
map<int64_t, BasicBlock *> IDToBlock;

inline shared_ptr<ControlNode> make_new_node(ControlGraph& cg)
{
    auto newNode = make_shared<ControlNode>();
    newNode->originalBlocks.push_back(newNode->NID);
    cg.addNode(newNode);
    return newNode;
}

inline void make_unconditional_edge(ControlGraph& cg, uint64_t freq, shared_ptr<GraphNode> src, shared_ptr<GraphNode> snk)
{
    auto newEdge = make_shared<UnconditionalEdge>(freq, src, snk);
    src->addSuccessor(newEdge);
    snk->addPredecessor(newEdge);
    cg.addEdge(newEdge);
}

inline void make_conditional_edge(ControlGraph& cg, uint64_t freq, uint64_t weight, shared_ptr<ControlNode>& src, shared_ptr<ControlNode>& snk)
{
    auto newEdge = make_shared<ConditionalEdge>(freq, src, snk);
    newEdge->setWeight(weight);
    src->addSuccessor(newEdge);
    snk->addPredecessor(newEdge);
    cg.addEdge(newEdge);
}

ControlGraph PrepFirstTest()
{
    // the subgraph in this test is a loop with a fork
    // the correct answer is for the loop to be transformed into a single node that loops upon itself
    ControlGraph graph;
    auto start = make_new_node(graph);
    auto zero = make_new_node(graph);
    auto one = make_new_node(graph);
    auto two = make_new_node(graph);
    auto three = make_new_node(graph);
    auto four = make_new_node(graph);
    auto five = make_new_node(graph);
    auto six = make_new_node(graph);
    auto seven = make_new_node(graph);
    auto eight = make_new_node(graph);
    auto nine = make_new_node(graph);
    auto ten = make_new_node(graph);
    auto eleven = make_new_node(graph);
    auto twelve = make_new_node(graph);
    auto thirteen = make_new_node(graph);
    auto fourteen = make_new_node(graph);
    auto fifteen = make_new_node(graph);
    auto sixteen = make_new_node(graph);
    auto seventeen = make_new_node(graph);
    auto eighteen = make_new_node(graph);
    auto nineteen = make_new_node(graph);
    auto twenty = make_new_node(graph);
    auto twentyone = make_new_node(graph);
    auto twentytwo = make_new_node(graph);
    auto twentythree = make_new_node(graph);
    auto end = make_new_node(graph);

    make_conditional_edge(graph, 33, 100, start, zero);
    make_conditional_edge(graph, 33, 100, start, one);
    make_conditional_edge(graph, 34, 100, start, two);

    make_unconditional_edge(graph, 33, zero, three);

    make_unconditional_edge(graph, 33, one, three);

    make_unconditional_edge(graph, 34, two, three);

    make_conditional_edge(graph, 50, 100, three, four);
    make_conditional_edge(graph, 50, 100, three, five);

    make_conditional_edge(graph, 30, 50, four, six);
    make_conditional_edge(graph, 20, 50, four, seven);

    make_unconditional_edge(graph, 50, five, eleven);

    make_conditional_edge(graph, 20, 30, six, eight);
    make_conditional_edge(graph, 10, 30, six, nine);

    make_unconditional_edge(graph, 20, seven, ten);

    make_unconditional_edge(graph, 20, eight, eleven);

    make_unconditional_edge(graph, 10, nine, ten);

    make_unconditional_edge(graph, 30, ten, eleven);

    make_conditional_edge(graph, 33, 100, eleven, twelve);
    make_conditional_edge(graph, 33, 100, eleven, thirteen);
    make_conditional_edge(graph, 34, 100, eleven, fourteen);

    make_unconditional_edge(graph, 33, twelve, fifteen);

    make_unconditional_edge(graph, 33, thirteen, fifteen);

    make_unconditional_edge(graph, 34, fourteen, fifteen);

    make_conditional_edge(graph, 50, 100, fifteen, sixteen);
    make_conditional_edge(graph, 50, 100, fifteen, seventeen);

    make_conditional_edge(graph, 30, 50, sixteen, eighteen);
    make_conditional_edge(graph, 20, 50, sixteen, nineteen);

    make_unconditional_edge(graph, 50, seventeen, twentythree);

    make_conditional_edge(graph, 20, 30, eighteen, twenty);
    make_conditional_edge(graph, 10, 30, eighteen, twentyone);

    make_unconditional_edge(graph, 20, nineteen, twentytwo);

    make_unconditional_edge(graph, 20, twenty, twentythree);

    make_unconditional_edge(graph, 10, twentyone, twentytwo);

    make_unconditional_edge(graph, 30, twentytwo, twentythree);

    make_conditional_edge(graph,  1, 100, twentythree, end);
    make_conditional_edge(graph, 99, 100, twentythree, eleven);

    return graph;
}

/// Implements a series of checks on a transformed graph
/// 1. The graph should have at least one node in it
/// 2. For each node, all predecessors and successors should be present in the graph
/// 3. The graph should not have any breakaway sections, ie only one node in the graph should have no predecessors, and only one node should have no successors
/// 4. There should be the same number of cycles in the original graph as their should be the transformed one
/// 5. For a given node, all outgoing edge probabilities (weights) should sum to one
/// 6. Test case where the correct answer is a single node... and no edges
// John: ... but how can we check whether the graph is as simple as it should be?
//  - use teeball cases
// John: on tricks to express problem graphs quickly
//  -
void Checks(ControlGraph &original, ControlGraph &transformed, string step)
{
    // 1. the graphs should not be empty
    if (transformed.empty() && !original.empty())
    {
        throw AtlasException(step + ": Transformed graph is empty!");
    }
    // 2. all preds and succs should be present
    for (const auto node : transformed.getControlNodes())
    {
        for (auto pred : node->getPredecessors())
        {
            if (!transformed.find(pred))
            {
                throw AtlasException(step + ": Predecessor edge missing!");
            }
            if (!transformed.find(pred->getSrc()))
            {
                throw AtlasException(step + ": Predecessor source missing!");
            }
            if (!transformed.find(pred->getSnk()))
            {
                throw AtlasException(step + ": Predecessor sink missing!");
            }
        }
        for (auto succ : node->getSuccessors())
        {
            if (!transformed.find(succ))
            {
                throw AtlasException(step + ": Successor missing!");
            }
            if (!transformed.find(succ->getSrc()))
            {
                throw AtlasException(step + ": Successor source missing!");
            }
            if (!transformed.find(succ->getSnk()))
            {
                throw AtlasException(step + ": Successor sink missing!");
            }
        }
    }
    // 3. The graph should be one complete piece
    // we check this by finding a "start" and "end" node (nodes with no preds, succs, respectively) and if there are more than one of these... wrong
    bool foundStart = false;
    bool foundEnd = false;
    for (auto node : transformed.nodes())
    {
        if (node->getPredecessors().empty())
        {
            if (foundStart)
            {
                throw AtlasException(step + ": Graph is not one whole piece!");
            }
            else
            {
                foundStart = true;
            }
        }
        else if (node->getSuccessors().empty())
        {
            if (foundEnd)
            {
                throw AtlasException(step + ": Graph is not one whole piece!");
            }
            else
            {
                foundEnd = true;
            }
        }
    }
    // 4. transforms don't destroy cycles
    // to implement this check, we either need an algorithm that returns each unique cycle, or we need to do kernel segmentation and compare the results between original and transformed
    // not done yet

    // 5. for each node, all outgoing edge probabilities sum to 1
    for (auto node : original.nodes())
    {
        if (node->getSuccessors().empty())
        {
            continue;
        }
        double sum = 0.0;
        for (auto succ : node->getSuccessors())
        {
            sum += succ->getProb();
        }
        if (sum < 0.9999 || sum > 1.0001)
        {
            throw AtlasException(step + ": Outgoing edges do not sum to 1!");
        }
    }
}

// case-specific checks
void Test1_Checks(const ControlGraph& original, const ControlGraph& transformed)
{
    // ending subgraph size should be two nodes, two edges
    if( transformed.node_count() != 14 || transformed.edge_count() != 18 )
    {
        throw AtlasException("Test 1 did not have the correct ending subgraph!");
    }
    // evaluate kernel entrances and exits
    for( const auto& node : transformed.nodes() )
    {
        if( auto mlc = dynamic_pointer_cast<MLCycle>(node) )
        {
            set<pair<int64_t, int64_t>> entrances;
            spdlog::info("First test - virtual node entrances:");
            for( const auto& ent : mlc->getEntrances() )
            {
                spdlog::info(to_string(ent->getSrc()->NID) + " -> " + to_string(ent->getSnk()->NID));
                auto entPairs = findOriginalBlockIDs(ent, true);
                entrances.insert(entPairs.begin(), entPairs.end());
            }
            spdlog::info("First test - original block entrances:");
            for( const auto& ent : entrances )
            {
                spdlog::info(to_string(ent.first) + " -> "+to_string(ent.second));
            }
            if( entrances.size() != 3 )
            {
                throw AtlasException("Test 1: Wrong number of kernel entrances!")
            }
            bool foundFirst = false;
            bool foundSecond = false;
            bool foundThird = false;
            auto entIt = entrances.begin();
            while( entIt != entrances.end() )
            {
                if( (entIt->first == 6) && (entIt->second == 12) )
                {
                    foundFirst = true;
                }
                else if( (entIt->first == 9) && (entIt->second == 12) )
                {
                    foundSecond = true;
                }
                else if( (entIt->first == 11) && (entIt->second == 12) )
                {
                    foundThird = true;
                }
                entIt = next(entIt);
            }
            if( !foundFirst )
            {
                throw AtlasException("Test 1: Kernel did not have required 6->12 entrance!");
            }
            else if( !foundSecond )
            {
                throw AtlasException("Test 1: Kernel did not have required 9->12 entrance!");
            }
            else if( !foundThird )
            {
                throw AtlasException("Test 1: Kernel did not have required 11->12 entrance!");
            }

            set<pair<int64_t, int64_t>> exits;
            for( const auto& ex : mlc->getExits() )
            {
                auto exPairs = findOriginalBlockIDs(ex, true);
                exits.insert(exPairs.begin(), exPairs.end());
            }
            if( exits.size() != 1 )
            {
                throw AtlasException("Test 1: Wrong number of kernel exits!")
            }
            bool found24to25 = false;
            if( exits.begin()->first != 24 || exits.begin()->second != 25 )
            {
                throw AtlasException("Test 1: Kernel did not have required 24->25 exit!");
            }
        }
    }
}

void ReverseTransformCheck(ControlGraph original, ControlGraph transformed, string step)
{
    reverseTransform(transformed);
    for (auto node : transformed.nodes())
    {
        if (original.find(node))
        {
            throw AtlasException(step + ": Node in transformed graph not found in original!");
        }
        auto origNode = original.getNode(node->NID);
        for (auto pred : node->getPredecessors())
        {
            if (origNode->getPredecessors().find(pred) == origNode->getPredecessors().end())
            {
                throw AtlasException(step + ": Predecessor in transformed graph not found in equivalent original node predecessors!");
            }
        }
        for (auto succ : node->getSuccessors())
        {
            if (origNode->getSuccessors().find(succ) == origNode->getSuccessors().end())
            {
                throw AtlasException(step + ": Successor in transformed graph not found in equivalent original node successors!");
            }
        }
    }
    for (auto node : original.nodes())
    {
        if (original.find(node))
        {
            throw AtlasException(step + ": Node in original graph not found in transformed!");
        }
        auto transformedNode = original.getNode(node->NID);
        for (auto pred : node->getPredecessors())
        {
            if (transformedNode->getPredecessors().find(pred) == transformedNode->getPredecessors().end())
            {
                throw AtlasException(step + ": Predecessor in original graph not found in equivalent transformed node predecessors!");
            }
        }
        for (auto succ : node->getSuccessors())
        {
            if (transformedNode->getSuccessors().find(succ) == transformedNode->getSuccessors().end())
            {
                throw AtlasException(step + ": Successor in original graph not found in equivalent transformed node successors!");
            }
        }
    }
}

uint8_t RunTest(ControlGraph (*testprep)(void), string name)
{
    auto transformed = testprep();
    auto original    = testprep();
    string DotString = "\n# Original Graph\n";
    ofstream LastTransform("OriginalGraph_"+name+".dot");
    DotString += GenerateDot(transformed, true);
    LastTransform << DotString << "\n";
    LastTransform.close();
    try
    {
        ApplyCFGTransforms(transformed, Cyclebite::Graph::CallGraph(), false);
        FindMLCycles(transformed, Cyclebite::Graph::CallGraph(), true);
        Checks(original, transformed, name);
        if( name == "Test1" )
        {
            Test1_Checks(original, transformed);
        }
    }
    catch(AtlasException& e)
    {
        std::cerr << e.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

int main()
{
    spdlog::info("Running test 5");
    if (RunTest(PrepFirstTest, "Test1"))
    {
        return EXIT_FAILURE;
    }

    spdlog::info("Transforms pass all tests!");
    return EXIT_SUCCESS;
}