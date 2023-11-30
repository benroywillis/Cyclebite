//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
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
    // this function creates a subgraph of three nodes that forms a sequential chain, and the tail loops back to the head
    // the correct transformed graph is just a single node that loops onto itself

    auto zero = make_shared<ControlNode>(0);
    auto one = make_shared<ControlNode>(1);
    auto two = make_shared<ControlNode>(2);
    auto start = make_shared<ControlNode>(3);

    auto start_zero = make_shared<UnconditionalEdge>(1, start, zero);
    start->addSuccessor(start_zero);
    zero->addPredecessor(start_zero);

    auto zero_one = make_shared<UnconditionalEdge>(1, zero, one);
    zero->addSuccessor(zero_one);
    one->addPredecessor(zero_one);

    auto two_zero = make_shared<UnconditionalEdge>(1, two, zero);
    zero->addPredecessor(two_zero);
    two->addSuccessor(two_zero);

    auto one_two = make_shared<UnconditionalEdge>(1, one, two);
    one->addSuccessor(one_two);
    two->addPredecessor(one_two);

    ControlGraph graph;
    graph.addNode(start);
    graph.addNode(zero);
    graph.addNode(one);
    graph.addNode(two);
    graph.addNode(start);
    graph.addEdge(start_zero);
    graph.addEdge(zero_one);
    graph.addEdge(one_two);
    graph.addEdge(two_zero);
    return graph;
}

ControlGraph PrepSecondTest()
{
    // the subgraph in this test is a loop with a fork
    // the correct answer is for the loop to be transformed into a single node that loops upon itself
    auto zero = make_shared<ControlNode>(0);
    auto one = make_shared<ControlNode>(1);
    auto two = make_shared<ControlNode>(2);
    auto three = make_shared<ControlNode>(3);
    auto four = make_shared<ControlNode>(4);
    auto five = make_shared<ControlNode>(5);
    auto start = make_shared<ControlNode>(6);

    auto start_zero = make_shared<UnconditionalEdge>(1, start, zero);
    start->addSuccessor(start_zero);
    zero->addPredecessor(start_zero);

    auto zero_one = make_shared<ConditionalEdge>(9, zero, one);
    zero_one->setWeight(10);
    zero->addSuccessor(zero_one);
    one->addPredecessor(zero_one);

    auto zero_two = make_shared<ConditionalEdge>(1, zero, two);
    zero_two->setWeight(10);
    zero->addSuccessor(zero_two);
    two->addPredecessor(zero_two);

    auto one_three = make_shared<UnconditionalEdge>(9, one, three);
    one->addSuccessor(one_three);
    three->addPredecessor(one_three);

    auto two_four = make_shared<UnconditionalEdge>(1, two, four);
    two->addSuccessor(two_four);
    four->addPredecessor(two_four);

    auto three_five = make_shared<UnconditionalEdge>(9, three, five);
    three->addSuccessor(three_five);
    five->addPredecessor(three_five);

    auto four_five = make_shared<UnconditionalEdge>(1, four, five);
    four->addSuccessor(four_five);
    five->addPredecessor(four_five);

    auto five_zero = make_shared<UnconditionalEdge>(10, five, zero);
    five->addSuccessor(five_zero);
    zero->addPredecessor(five_zero);

    ControlGraph graph;
    graph.addNode(zero);
    graph.addNode(one);
    graph.addNode(two);
    graph.addNode(three);
    graph.addNode(four);
    graph.addNode(five);
    graph.addNode(start);
    graph.addEdge(start_zero);
    graph.addEdge(zero_one);
    graph.addEdge(zero_two);
    graph.addEdge(one_three);
    graph.addEdge(three_five);
    graph.addEdge(two_four);
    graph.addEdge(four_five);
    graph.addEdge(five_zero);
    return graph;
}

ControlGraph PrepThirdTest()
{
    // this function creates a subgraph of 9 nodes that form a subgraph that should be ripe for the fanin-fanout transform
    // the correct transformed graph is a single node that contains all other nodes in the graph

    // node 1 goes directly to the sink node
    auto zero = make_shared<ControlNode>(0);
    auto one = make_shared<ControlNode>(1);
    auto two = make_shared<ControlNode>(2);
    auto three = make_shared<ControlNode>(3);
    auto four = make_shared<ControlNode>(4);
    auto five = make_shared<ControlNode>(5);
    auto six = make_shared<ControlNode>(6);
    auto seven = make_shared<ControlNode>(7);
    auto eight = make_shared<ControlNode>(8);
    auto nine = make_shared<ControlNode>(9);
    auto start = make_shared<ControlNode>(10);

    auto start_zero = make_shared<UnconditionalEdge>(1, start, zero);
    start->addSuccessor(start_zero);
    zero->addPredecessor(start_zero);

    auto zero_one = make_shared<ConditionalEdge>(9900, zero, one);
    zero_one->setWeight(10000);
    auto zero_two = make_shared<ConditionalEdge>(100, zero, two);
    zero_two->setWeight(10000);
    auto nine_zero = make_shared<UnconditionalEdge>(10000, nine, zero);
    zero->addSuccessor(zero_one);
    zero->addSuccessor(zero_two);
    zero->addPredecessor(nine_zero);

    auto one_eight = make_shared<UnconditionalEdge>(9900, one, eight);
    one->addSuccessor(one_eight);
    one->addPredecessor(zero_one);

    auto two_three = make_shared<ConditionalEdge>(51, two, three);
    two_three->setWeight(100);
    auto two_four = make_shared<ConditionalEdge>(49, two, four);
    two_four->setWeight(100);
    two->addSuccessor(two_three);
    two->addSuccessor(two_four);
    two->addPredecessor(zero_two);

    auto three_five = make_shared<ConditionalEdge>(2, three, five);
    three_five->setWeight(51);
    auto three_six = make_shared<ConditionalEdge>(49, three, six);
    three_six->setWeight(51);
    three->addSuccessor(three_five);
    three->addSuccessor(three_six);
    three->addPredecessor(two_three);

    auto four_seven = make_shared<UnconditionalEdge>(49, four, seven);
    four->addSuccessor(four_seven);
    four->addPredecessor(two_four);

    auto five_seven = make_shared<UnconditionalEdge>(2, five, seven);
    five->addSuccessor(five_seven);
    five->addPredecessor(three_five);

    auto six_eight = make_shared<UnconditionalEdge>(49, six, eight);
    six->addSuccessor(six_eight);
    six->addPredecessor(three_six);

    auto seven_eight = make_shared<UnconditionalEdge>(51, seven, eight);
    seven->addSuccessor(seven_eight);
    seven->addPredecessor(four_seven);
    seven->addPredecessor(five_seven);

    auto eight_nine = make_shared<UnconditionalEdge>(10000, eight, nine);
    eight->addSuccessor(eight_nine);
    eight->addPredecessor(one_eight);
    eight->addPredecessor(six_eight);
    eight->addPredecessor(seven_eight);

    nine->addSuccessor(nine_zero);
    nine->addPredecessor(eight_nine);

    ControlGraph graph;
    graph.addNode(zero);
    graph.addNode(one);
    graph.addNode(two);
    graph.addNode(three);
    graph.addNode(four);
    graph.addNode(five);
    graph.addNode(six);
    graph.addNode(seven);
    graph.addNode(eight);
    graph.addNode(nine);
    graph.addNode(start);
    graph.addEdge(start_zero);
    graph.addEdge(zero_one);
    graph.addEdge(zero_two);
    graph.addEdge(nine_zero);
    graph.addEdge(one_eight);
    graph.addEdge(two_three);
    graph.addEdge(two_four);
    graph.addEdge(three_five);
    graph.addEdge(three_six);
    graph.addEdge(four_seven);
    graph.addEdge(five_seven);
    graph.addEdge(six_eight);
    graph.addEdge(seven_eight);
    graph.addEdge(eight_nine);
    return graph;
}

ControlGraph PrepSharedFunctionTest()
{
    auto node0 = make_shared<ControlNode>(0);
    auto node1 = make_shared<ControlNode>(1);
    auto node2 = make_shared<ControlNode>(2);
    auto node3 = make_shared<ControlNode>(3);
    auto node4 = make_shared<ControlNode>(4);
    auto node5 = make_shared<ControlNode>(5);
    auto node6 = make_shared<ControlNode>(6);
    auto node7 = make_shared<ControlNode>(7);
    auto node8 = make_shared<ControlNode>(8);
    auto node9 = make_shared<ControlNode>(9);
    auto node10 = make_shared<ControlNode>(10);
    auto node11 = make_shared<ControlNode>(11);
    auto node12 = make_shared<ControlNode>(12);
    auto node13 = make_shared<ControlNode>(13);
    auto node14 = make_shared<ControlNode>(14);
    auto node15 = make_shared<ControlNode>(15);
    auto node16 = make_shared<ControlNode>(16);
    auto node17 = make_shared<ControlNode>(17);
    auto node18 = make_shared<ControlNode>(18);
    auto node19 = make_shared<ControlNode>(19);
    auto node20 = make_shared<ControlNode>(20);
    auto node21 = make_shared<ControlNode>(21);
    auto node22 = make_shared<ControlNode>(22);
    auto node23 = make_shared<ControlNode>(23);
    auto node24 = make_shared<ControlNode>(24);
    auto node25 = make_shared<ControlNode>(25);
    auto node26 = make_shared<ControlNode>(26);
    auto node27 = make_shared<ControlNode>(27);
    auto node28 = make_shared<ControlNode>(28);
    auto node29 = make_shared<ControlNode>(29);
    auto node30 = make_shared<ControlNode>(30);
    auto node31 = make_shared<ControlNode>(31);
    auto node32 = make_shared<ControlNode>(32);
    auto node33 = make_shared<ControlNode>(33);
    auto node34 = make_shared<ControlNode>(34);
    auto node35 = make_shared<ControlNode>(35);
    auto node36 = make_shared<ControlNode>(36);
    auto node37 = make_shared<ControlNode>(37);
    auto node38 = make_shared<ControlNode>(38);
    auto node39 = make_shared<ControlNode>(39);
    auto node40 = make_shared<ControlNode>(40);
    auto node41 = make_shared<ControlNode>(41);
    auto node42 = make_shared<ControlNode>(42);
    auto node43 = make_shared<ControlNode>(43);
    auto node44 = make_shared<ControlNode>(44);
    auto node45 = make_shared<ControlNode>(45);
    auto node46 = make_shared<ControlNode>(46);
    auto node47 = make_shared<ControlNode>(47);
    auto node48 = make_shared<ControlNode>(48);
    auto node49 = make_shared<ControlNode>(49);

    auto edge0 = make_shared<ConditionalEdge>(128, node0, node1);
    edge0->setWeight(16512);
    node0->addSuccessor(edge0);
    node1->addPredecessor(edge0);
    auto edge22 = make_shared<ConditionalEdge>(16384, node0, node15);
    edge22->setWeight(16512);
    node0->addSuccessor(edge22);
    node15->addPredecessor(edge22);
    auto edge23 = make_shared<UnconditionalEdge>(128, node1, node27);
    node1->addSuccessor(edge23);
    node27->addPredecessor(edge23);
    auto edge1 = make_shared<UnconditionalEdge>(121, node2, node3);
    node2->addSuccessor(edge1);
    node3->addPredecessor(edge1);
    auto edge12 = make_shared<ConditionalEdge>(1, node3, node22);
    edge12->setWeight(122);
    node3->addSuccessor(edge12);
    node22->addPredecessor(edge12);
    auto edge34 = make_shared<ConditionalEdge>(121, node3, node33);
    edge34->setWeight(122);
    node3->addSuccessor(edge34);
    node33->addPredecessor(edge34);
    auto edge2 = make_shared<ConditionalEdge>(16384, node4, node5);
    edge2->setWeight(16512);
    node4->addSuccessor(edge2);
    node5->addPredecessor(edge2);
    auto edge59 = make_shared<ConditionalEdge>(128, node4, node49);
    edge59->setWeight(16512);
    node4->addSuccessor(edge59);
    node49->addPredecessor(edge59);
    auto edge49 = make_shared<UnconditionalEdge>(16384, node5, node40);
    node5->addSuccessor(edge49);
    node40->addPredecessor(edge49);
    auto edge3 = make_shared<ConditionalEdge>(14641, node6, node7);
    edge3->setWeight(14762);
    node6->addSuccessor(edge3);
    node7->addPredecessor(edge3);
    auto edge45 = make_shared<ConditionalEdge>(121, node6, node12);
    edge45->setWeight(14762);
    node6->addSuccessor(edge45);
    node12->addPredecessor(edge45);
    auto edge41 = make_shared<UnconditionalEdge>(14641, node7, node8);
    node7->addSuccessor(edge41);
    node8->addPredecessor(edge41);
    auto edge4 = make_shared<UnconditionalEdge>(29282, node8, node9);
    node8->addSuccessor(edge4);
    node9->addPredecessor(edge4);
    auto edge13 = make_shared<ConditionalEdge>(29282, node9, node23);
    edge13->setWeight(263538);
    node9->addSuccessor(edge13);
    node23->addPredecessor(edge13);
    auto edge52 = make_shared<ConditionalEdge>(234256, node9, node41);
    edge52->setWeight(263538);
    node9->addSuccessor(edge52);
    node41->addPredecessor(edge52);
    auto edge5 = make_shared<UnconditionalEdge>(1874048, node10, node11);
    node10->addSuccessor(edge5);
    node11->addPredecessor(edge5);
    auto edge11 = make_shared<UnconditionalEdge>(1874048, node11, node21);
    node11->addSuccessor(edge11);
    node21->addPredecessor(edge11);
    auto edge6 = make_shared<UnconditionalEdge>(121, node12, node2);
    node12->addSuccessor(edge6);
    node2->addPredecessor(edge6);
    auto edge7 = make_shared<UnconditionalEdge>(1, node13, node14);
    node13->addSuccessor(edge7);
    node14->addPredecessor(edge7);
    auto edge27 = make_shared<ConditionalEdge>(1, node14, node36);
    edge27->setWeight(129);
    node14->addSuccessor(edge27);
    node36->addPredecessor(edge27);
    auto edge53 = make_shared<ConditionalEdge>(128, node14, node44);
    edge53->setWeight(129);
    node14->addSuccessor(edge53);
    node44->addPredecessor(edge53);
    auto edge8 = make_shared<UnconditionalEdge>(16384, node15, node16);
    node15->addSuccessor(edge8);
    node16->addPredecessor(edge8);
    auto edge54 = make_shared<UnconditionalEdge>(16384, node16, node47);
    node16->addSuccessor(edge54);
    node47->addPredecessor(edge54);
    auto edge9 = make_shared<ConditionalEdge>(121, node17, node18);
    edge9->setWeight(122);
    node17->addSuccessor(edge9);
    node18->addPredecessor(edge9);
    auto edge42 = make_shared<ConditionalEdge>(1, node17, node46);
    edge42->setWeight(122);
    node17->addSuccessor(edge42);
    node46->addPredecessor(edge42);
    auto edge55 = make_shared<UnconditionalEdge>(121, node18, node25);
    node18->addSuccessor(edge55);
    node25->addPredecessor(edge55);
    auto edge10 = make_shared<UnconditionalEdge>(14641, node19, node20);
    node19->addSuccessor(edge10);
    node20->addPredecessor(edge10);
    auto edge57 = make_shared<UnconditionalEdge>(14641, node20, node25);
    node20->addSuccessor(edge57);
    node25->addPredecessor(edge57);
    auto edge21 = make_shared<ConditionalEdge>(1874048, node21, node10);
    edge21->setWeight(2108304);
    node21->addSuccessor(edge21);
    node10->addPredecessor(edge21);
    auto edge46 = make_shared<ConditionalEdge>(234256, node21, node42);
    edge46->setWeight(2108304);
    node21->addSuccessor(edge46);
    node42->addPredecessor(edge46);
    auto edge25 = make_shared<UnconditionalEdge>(1, node22, node13);
    node22->addSuccessor(edge25);
    node13->addPredecessor(edge25);
    auto edge29 = make_shared<ConditionalEdge>(14641, node23, node19);
    edge29->setWeight(29282);
    node23->addSuccessor(edge29);
    node19->addPredecessor(edge29);
    auto edge14 = make_shared<ConditionalEdge>(14641, node23, node24);
    edge14->setWeight(29282);
    node23->addSuccessor(edge14);
    node24->addPredecessor(edge14);
    auto edge37 = make_shared<UnconditionalEdge>(14641, node24, node37);
    node24->addSuccessor(edge37);
    node37->addPredecessor(edge37);
    auto edge15 = make_shared<ConditionalEdge>(121, node25, node26);
    edge15->setWeight(14762);
    node25->addSuccessor(edge15);
    node26->addPredecessor(edge15);
    auto edge39 = make_shared<ConditionalEdge>(14641, node25, node45);
    edge39->setWeight(14762);
    node25->addSuccessor(edge39);
    node45->addPredecessor(edge39);
    auto edge40 = make_shared<UnconditionalEdge>(121, node26, node38);
    node26->addSuccessor(edge40);
    node38->addPredecessor(edge40);
    auto edge16 = make_shared<UnconditionalEdge>(128, node27, node14);
    node27->addSuccessor(edge16);
    node14->addPredecessor(edge16);
    auto edge17 = make_shared<UnconditionalEdge>(1, node28, node29);
    node28->addSuccessor(edge17);
    node29->addPredecessor(edge17);
    auto edge51 = make_shared<UnconditionalEdge>(1, node29, node3);
    node29->addSuccessor(edge51);
    node3->addPredecessor(edge51);
    auto edge18 = make_shared<UnconditionalEdge>(128, node30, node4);
    node30->addSuccessor(edge18);
    node4->addPredecessor(edge18);
    auto edge19 = make_shared<UnconditionalEdge>(16384, node31, node32);
    node31->addSuccessor(edge19);
    node32->addPredecessor(edge19);
    auto edge58 = make_shared<UnconditionalEdge>(16384, node32, node4);
    node32->addSuccessor(edge58);
    node4->addPredecessor(edge58);
    auto edge20 = make_shared<UnconditionalEdge>(121, node33, node6);
    node33->addSuccessor(edge20);
    node6->addPredecessor(edge20);
    auto edge24 = make_shared<UnconditionalEdge>(1, node34, node17);
    node34->addSuccessor(edge24);
    node17->addPredecessor(edge24);
    auto edge26 = make_shared<ConditionalEdge>(1, node35, node28);
    edge26->setWeight(129);
    node35->addSuccessor(edge26);
    node28->addPredecessor(edge26);
    auto edge32 = make_shared<ConditionalEdge>(128, node35, node30);
    edge32->setWeight(129);
    node35->addSuccessor(edge32);
    node30->addPredecessor(edge32);
    auto edge47 = make_shared<UnconditionalEdge>(1, node36, node34);
    node36->addSuccessor(edge47);
    node34->addPredecessor(edge47);
    auto edge28 = make_shared<UnconditionalEdge>(14641, node37, node6);
    node37->addSuccessor(edge28);
    node6->addPredecessor(edge28);
    auto edge30 = make_shared<UnconditionalEdge>(121, node38, node17);
    node38->addSuccessor(edge30);
    node17->addPredecessor(edge30);
    auto edge31 = make_shared<UnconditionalEdge>(128, node39, node35);
    node39->addSuccessor(edge31);
    node35->addPredecessor(edge31);
    auto edge33 = make_shared<UnconditionalEdge>(16384, node40, node31);
    node40->addSuccessor(edge33);
    node31->addPredecessor(edge33);
    auto edge35 = make_shared<UnconditionalEdge>(234256, node41, node21);
    node41->addSuccessor(edge35);
    node21->addPredecessor(edge35);
    auto edge36 = make_shared<UnconditionalEdge>(234256, node42, node43);
    node42->addSuccessor(edge36);
    node43->addPredecessor(edge36);
    auto edge44 = make_shared<UnconditionalEdge>(234256, node43, node9);
    node43->addSuccessor(edge44);
    node9->addPredecessor(edge44);
    auto edge38 = make_shared<UnconditionalEdge>(128, node44, node0);
    node44->addSuccessor(edge38);
    node0->addPredecessor(edge38);
    auto edge56 = make_shared<UnconditionalEdge>(14641, node45, node8);
    node45->addSuccessor(edge56);
    node8->addPredecessor(edge56);
    auto edge43 = make_shared<UnconditionalEdge>(16384, node47, node0);
    node47->addSuccessor(edge43);
    node0->addPredecessor(edge43);
    auto edge48 = make_shared<UnconditionalEdge>(1, node48, node35);
    node48->addSuccessor(edge48);
    node35->addPredecessor(edge48);
    auto edge50 = make_shared<UnconditionalEdge>(128, node49, node39);
    node49->addSuccessor(edge50);
    node39->addPredecessor(edge50);

    ControlGraph subgraph;
    subgraph.addNode(node0);
    subgraph.addNode(node1);
    subgraph.addNode(node2);
    subgraph.addNode(node3);
    subgraph.addNode(node4);
    subgraph.addNode(node5);
    subgraph.addNode(node6);
    subgraph.addNode(node7);
    subgraph.addNode(node8);
    subgraph.addNode(node9);
    subgraph.addNode(node10);
    subgraph.addNode(node11);
    subgraph.addNode(node12);
    subgraph.addNode(node13);
    subgraph.addNode(node14);
    subgraph.addNode(node15);
    subgraph.addNode(node16);
    subgraph.addNode(node17);
    subgraph.addNode(node18);
    subgraph.addNode(node19);
    subgraph.addNode(node20);
    subgraph.addNode(node21);
    subgraph.addNode(node22);
    subgraph.addNode(node23);
    subgraph.addNode(node24);
    subgraph.addNode(node25);
    subgraph.addNode(node26);
    subgraph.addNode(node27);
    subgraph.addNode(node28);
    subgraph.addNode(node29);
    subgraph.addNode(node30);
    subgraph.addNode(node31);
    subgraph.addNode(node32);
    subgraph.addNode(node33);
    subgraph.addNode(node34);
    subgraph.addNode(node35);
    subgraph.addNode(node36);
    subgraph.addNode(node37);
    subgraph.addNode(node38);
    subgraph.addNode(node39);
    subgraph.addNode(node40);
    subgraph.addNode(node41);
    subgraph.addNode(node42);
    subgraph.addNode(node43);
    subgraph.addNode(node44);
    subgraph.addNode(node45);
    subgraph.addNode(node46);
    subgraph.addNode(node47);
    subgraph.addNode(node48);
    subgraph.addNode(node49);
    subgraph.addEdge(edge0);
    subgraph.addEdge(edge22);
    subgraph.addEdge(edge23);
    subgraph.addEdge(edge1);
    subgraph.addEdge(edge12);
    subgraph.addEdge(edge34);
    subgraph.addEdge(edge2);
    subgraph.addEdge(edge59);
    subgraph.addEdge(edge49);
    subgraph.addEdge(edge3);
    subgraph.addEdge(edge45);
    subgraph.addEdge(edge41);
    subgraph.addEdge(edge4);
    subgraph.addEdge(edge13);
    subgraph.addEdge(edge52);
    subgraph.addEdge(edge5);
    subgraph.addEdge(edge11);
    subgraph.addEdge(edge6);
    subgraph.addEdge(edge7);
    subgraph.addEdge(edge27);
    subgraph.addEdge(edge53);
    subgraph.addEdge(edge8);
    subgraph.addEdge(edge54);
    subgraph.addEdge(edge9);
    subgraph.addEdge(edge42);
    subgraph.addEdge(edge55);
    subgraph.addEdge(edge10);
    subgraph.addEdge(edge57);
    subgraph.addEdge(edge21);
    subgraph.addEdge(edge46);
    subgraph.addEdge(edge25);
    subgraph.addEdge(edge29);
    subgraph.addEdge(edge14);
    subgraph.addEdge(edge37);
    subgraph.addEdge(edge15);
    subgraph.addEdge(edge39);
    subgraph.addEdge(edge40);
    subgraph.addEdge(edge16);
    subgraph.addEdge(edge17);
    subgraph.addEdge(edge51);
    subgraph.addEdge(edge18);
    subgraph.addEdge(edge19);
    subgraph.addEdge(edge58);
    subgraph.addEdge(edge20);
    subgraph.addEdge(edge24);
    subgraph.addEdge(edge26);
    subgraph.addEdge(edge32);
    subgraph.addEdge(edge47);
    subgraph.addEdge(edge28);
    subgraph.addEdge(edge30);
    subgraph.addEdge(edge31);
    subgraph.addEdge(edge33);
    subgraph.addEdge(edge35);
    subgraph.addEdge(edge36);
    subgraph.addEdge(edge44);
    subgraph.addEdge(edge38);
    subgraph.addEdge(edge56);
    subgraph.addEdge(edge43);
    subgraph.addEdge(edge48);
    subgraph.addEdge(edge50);
    ofstream Original("SharedFunctionGraph.dot");
    auto OriginalGraph = GenerateDot(subgraph);
    Original << OriginalGraph << "\n";
    Original.close();
    return subgraph;
}

ControlGraph PrepFourthTest()
{
    // the subgraph in this test is a loop with a fork
    // the correct answer is for the loop to be transformed into a single node that loops upon itself
    auto zero = make_shared<ControlNode>(0);
    auto one = make_shared<ControlNode>(1);
    auto two = make_shared<ControlNode>(2);
    auto three = make_shared<ControlNode>(3);
    auto four = make_shared<ControlNode>(4);
    auto five = make_shared<ControlNode>(5);
    auto six = make_shared<ControlNode>(6);
    auto seven = make_shared<ControlNode>(7);
    auto eight = make_shared<ControlNode>(8);
    auto nine = make_shared<ControlNode>(9);
    auto ten = make_shared<ControlNode>(10);
    auto start = make_shared<ControlNode>(11);

    auto start_zero = make_shared<UnconditionalEdge>(1, start, zero);
    start->addSuccessor(start_zero);
    zero->addPredecessor(start_zero);

    auto zero_one = make_shared<UnconditionalEdge>(100, zero, one);
    zero->addSuccessor(zero_one);
    one->addPredecessor(zero_one);

    auto one_two = make_shared<ConditionalEdge>(50, one, two);
    one_two->setWeight(100);
    one->addSuccessor(one_two);
    two->addPredecessor(one_two);

    auto one_five = make_shared<ConditionalEdge>(50, one, five);
    one_five->setWeight(100);
    one->addSuccessor(one_five);
    five->addPredecessor(one_five);

    auto two_three = make_shared<ConditionalEdge>(10, two, three);
    two_three->setWeight(50);
    two->addSuccessor(two_three);
    three->addPredecessor(two_three);

    auto two_four = make_shared<ConditionalEdge>(40, two, four);
    two_four->setWeight(50);
    two->addSuccessor(two_four);
    four->addPredecessor(two_four);

    auto three_six = make_shared<UnconditionalEdge>(10, three, six);
    three->addSuccessor(three_six);
    six->addPredecessor(three_six);

    auto four_six = make_shared<UnconditionalEdge>(40, four, six);
    four->addSuccessor(four_six);
    six->addPredecessor(four_six);

    auto five_ten = make_shared<UnconditionalEdge>(50, five, ten);
    five->addSuccessor(five_ten);
    ten->addPredecessor(five_ten);

    auto six_seven = make_shared<ConditionalEdge>(44, six, seven);
    six_seven->setWeight(50);
    six->addSuccessor(six_seven);
    seven->addPredecessor(six_seven);

    auto six_eight = make_shared<ConditionalEdge>(5, six, eight);
    six_eight->setWeight(50);
    six->addSuccessor(six_eight);
    eight->addPredecessor(six_eight);

    auto six_ten = make_shared<ConditionalEdge>(1, six, ten);
    six_ten->setWeight(50);
    six->addSuccessor(six_ten);
    ten->addPredecessor(six_ten);

    auto seven_nine = make_shared<UnconditionalEdge>(45, seven, nine);
    seven->addSuccessor(seven_nine);
    nine->addPredecessor(seven_nine);

    auto eight_nine = make_shared<ConditionalEdge>(1, eight, nine);
    eight_nine->setWeight(5);
    eight->addSuccessor(eight_nine);
    nine->addPredecessor(eight_nine);

    auto eight_ten = make_shared<ConditionalEdge>(4, eight, ten);
    eight_ten->setWeight(5);
    eight->addSuccessor(eight_ten);
    ten->addPredecessor(eight_ten);

    auto nine_ten = make_shared<UnconditionalEdge>(46, nine, ten);
    nine->addSuccessor(nine_ten);
    ten->addPredecessor(nine_ten);

    auto ten_zero = make_shared<UnconditionalEdge>(100, ten, zero);
    ten->addSuccessor(ten_zero);
    zero->addPredecessor(ten_zero);

    ControlGraph graph;
    graph.addNode(zero);
    graph.addNode(one);
    graph.addNode(two);
    graph.addNode(three);
    graph.addNode(four);
    graph.addNode(five);
    graph.addNode(six);
    graph.addNode(seven);
    graph.addNode(eight);
    graph.addNode(nine);
    graph.addNode(ten);
    graph.addNode(start);
    graph.addEdge(start_zero);
    graph.addEdge(zero_one);
    graph.addEdge(one_two);
    graph.addEdge(one_five);
    graph.addEdge(two_three);
    graph.addEdge(two_four);
    graph.addEdge(three_six);
    graph.addEdge(four_six);
    graph.addEdge(five_ten);
    graph.addEdge(six_seven);
    graph.addEdge(six_eight);
    graph.addEdge(six_ten);
    graph.addEdge(seven_nine);
    graph.addEdge(eight_nine);
    graph.addEdge(eight_ten);
    graph.addEdge(nine_ten);
    graph.addEdge(ten_zero);
    return graph;
}

ControlGraph PrepFifthTest()
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

ControlGraph PrepSixthTest()
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
    auto end = make_new_node(graph);

    make_conditional_edge(graph, 90, 100, start, zero);
    make_conditional_edge(graph, 10, 100, start, nine);

    make_conditional_edge(graph, 10, 90, zero, one);
    make_conditional_edge(graph, 10, 90, zero, two);
    make_conditional_edge(graph, 10, 90, zero, three);
    make_conditional_edge(graph, 10, 90, zero, four);
    make_conditional_edge(graph, 10, 90, zero, five);
    make_conditional_edge(graph, 10, 90, zero, six);
    make_conditional_edge(graph, 10, 90, zero, seven);
    make_conditional_edge(graph, 10, 90, zero, eight);
    make_conditional_edge(graph, 10, 90, zero, eighteen);

    make_conditional_edge(graph, 5, 10, one, two);
    make_conditional_edge(graph, 5, 10, one, three);

    make_unconditional_edge(graph, 15, two, three);

    make_unconditional_edge(graph, 30, three, four);

    make_unconditional_edge(graph, 40, four, five);

    make_unconditional_edge(graph, 50, five, six);

    make_unconditional_edge(graph, 60, six, seven);

    make_unconditional_edge(graph, 70, seven, eight);

    make_unconditional_edge(graph, 80, eight, eighteen);

    make_conditional_edge(graph, 5, 10, nine, ten);
    make_conditional_edge(graph, 5, 10, nine, eleven);

    make_conditional_edge(graph, 2, 5, ten, twelve);
    make_conditional_edge(graph, 3, 5, ten, thirteen);

    make_unconditional_edge(graph, 5, eleven, seventeen);

    make_conditional_edge(graph, 500, 502, twelve, fourteen);
    make_conditional_edge(graph, 2, 502, twelve, fifteen);

    make_unconditional_edge(graph, 3, thirteen, sixteen);

    make_unconditional_edge(graph, 500, fourteen, twelve);

    make_unconditional_edge(graph, 2, fifteen, sixteen);

    make_unconditional_edge(graph, 2, sixteen, seventeen);

    make_unconditional_edge(graph, 10, seventeen, nineteen);

    make_unconditional_edge(graph, 90, eighteen, nineteen);

    make_unconditional_edge(graph, 10, nineteen, end);

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
        throw CyclebiteException(step + ": Transformed graph is empty!");
    }
    // 2. all preds and succs should be present
    for (const auto node : transformed.getControlNodes())
    {
        for (auto pred : node->getPredecessors())
        {
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
        for (auto succ : node->getSuccessors())
        {
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
                throw CyclebiteException(step + ": Graph is not one whole piece!");
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
                throw CyclebiteException(step + ": Graph is not one whole piece!");
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
            throw CyclebiteException(step + ": Outgoing edges do not sum to 1!");
        }
    }
}

// case-specific checks
void Test1_Checks(const ControlGraph& original, const ControlGraph& transformed)
{
    // ending subgraph size should be two nodes, two edges
    if( transformed.node_count() != 2 || transformed.edge_count() != 2 )
    {
        throw CyclebiteException("Test 1 did not have the correct ending subgraph!");
    }
}

void Test2_Checks(const ControlGraph& original, const ControlGraph& transformed)
{
    // ending subgraph size should be two nodes, two edges
    if( transformed.node_count() != 2 || transformed.edge_count() != 2 )
    {
        throw CyclebiteException("Test 2 did not have the correct ending subgraph!");
    }
}

void Test3_Checks(const ControlGraph& original, const ControlGraph& transformed)
{
    // ending subgraph size should be two nodes, two edges
    if( transformed.node_count() != 2 || transformed.edge_count() != 2 )
    {
        throw CyclebiteException("Test 3 did not have the correct ending subgraph!");
    }
}

void Test4_Checks(const ControlGraph& original, const ControlGraph& transformed)
{
    // ending subgraph size should be two nodes, two edges
    if( transformed.node_count() != 2 || transformed.edge_count() != 2 )
    {
        throw CyclebiteException("Test 4 did not have the correct ending subgraph!");
    }
}

void Test5_Checks(const ControlGraph& original, const ControlGraph& transformed)
{
    // ending subgraph size should be two nodes, two edges
    if( transformed.node_count() != 14 || transformed.edge_count() != 19 )
    {
        throw CyclebiteException("Test 5 did not have the correct ending subgraph!");
    }
}

void Test6_Checks(const ControlGraph& original, const ControlGraph& transformed)
{
    // ending subgraph size should be two nodes, two edges
    if( transformed.node_count() != 13 || transformed.edge_count() != 16 )
    {
        throw CyclebiteException("Test 6 did not have the correct ending subgraph!");
    }
}

void ReverseTransformCheck(ControlGraph original, ControlGraph transformed, string step)
{
    reverseTransform(transformed);
    for (auto node : transformed.nodes())
    {
        if (original.find(node))
        {
            throw CyclebiteException(step + ": Node in transformed graph not found in original!");
        }
        auto origNode = original.getNode(node->NID);
        for (auto pred : node->getPredecessors())
        {
            if (origNode->getPredecessors().find(pred) == origNode->getPredecessors().end())
            {
                throw CyclebiteException(step + ": Predecessor in transformed graph not found in equivalent original node predecessors!");
            }
        }
        for (auto succ : node->getSuccessors())
        {
            if (origNode->getSuccessors().find(succ) == origNode->getSuccessors().end())
            {
                throw CyclebiteException(step + ": Successor in transformed graph not found in equivalent original node successors!");
            }
        }
    }
    for (auto node : original.nodes())
    {
        if (original.find(node))
        {
            throw CyclebiteException(step + ": Node in original graph not found in transformed!");
        }
        auto transformedNode = original.getNode(node->NID);
        for (auto pred : node->getPredecessors())
        {
            if (transformedNode->getPredecessors().find(pred) == transformedNode->getPredecessors().end())
            {
                throw CyclebiteException(step + ": Predecessor in original graph not found in equivalent transformed node predecessors!");
            }
        }
        for (auto succ : node->getSuccessors())
        {
            if (transformedNode->getSuccessors().find(succ) == transformedNode->getSuccessors().end())
            {
                throw CyclebiteException(step + ": Successor in original graph not found in equivalent transformed node successors!");
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
    DotString += GenerateDot(transformed);
    LastTransform << DotString << "\n";
    LastTransform.close();
    try
    {
        ApplyCFGTransforms(transformed, Cyclebite::Graph::CallGraph(), false);
        Checks(original, transformed, name);
        if( name == "Test1" )
        {
            Test1_Checks(original, transformed);
        }
        else if( name == "Test2" )
        {
            Test2_Checks(original, transformed);
        }
        else if( name == "Test3" )
        {
            Test3_Checks(original, transformed);
        }
        else if( name == "Test4" )
        {
            Test4_Checks(original, transformed);
        }
        else if( name == "Test5" )
        {
            Test5_Checks(original, transformed);
        }
        else if( name == "Test6" )
        {
            Test6_Checks(original, transformed);
        }
    }
    catch(CyclebiteException& e)
    {
        std::cerr << e.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

int main()
{
    spdlog::info("Running test 1");
    if (RunTest(PrepFirstTest, "Test1"))
    {
        return EXIT_FAILURE;
    }

    spdlog::info("Running test 2");
    if (RunTest(PrepSecondTest, "Test2"))
    {
        return EXIT_FAILURE;
    }

    spdlog::info("Running test 3");
    if (RunTest(PrepThirdTest, "Test3"))
    {
        return EXIT_FAILURE;
    }

    spdlog::info("Running test 4");
    if (RunTest(PrepFourthTest, "Test4"))
    {
        return EXIT_FAILURE;
    }

    spdlog::info("Running test 5");
    if (RunTest(PrepFifthTest, "Test5"))
    {
        return EXIT_FAILURE;
    }

    spdlog::info("Running test 6");
    if (RunTest(PrepSixthTest, "Test6"))
    {
        return EXIT_FAILURE;
    }

    spdlog::info("Running SharedFunction test");
    if (RunTest(PrepSharedFunctionTest, "SharedFunctionTest"))
    {
        return EXIT_FAILURE;
    }

    spdlog::info("Transforms pass all tests!");
    return EXIT_SUCCESS;
}