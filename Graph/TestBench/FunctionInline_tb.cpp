#include "AtlasUtil/Exceptions.h"
#include "AtlasUtil/Format.h"
#include "AtlasUtil/IO.h"
#include "Graph.h"
#include "IO.h"
#include "Transforms.h"
#include <fstream>
#include <iostream>
#include <spdlog/spdlog.h>

/**
 * Implements elementary test cases for the four transforms used in the Cyclebyte structure extractiong toolchain
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

using namespace TraceAtlas::Graph;
using namespace std;
using namespace llvm;

// this block map is a patch to make the unit tests work with the existing check]
// the existing trivial transform has a rule that no function boundary can be crossed in the transform
// this map is used to check that
// thus we just populate the thing with one simple basic block such that the check is not exercised (yet)
LLVMContext fakeContext;
BasicBlock *simple = BasicBlock::Create(fakeContext);
map<int64_t, BasicBlock *> IDToBlock;

Graph PrepSharedFunctionTest()
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

    auto edge0 = make_shared<UnconditionalEdge>(128, node0, node1);
    edge0->setWeight(16512);
    node0->addSuccessor(edge0);
    node1->addPredecessor(edge0);
    auto edge22 = make_shared<UnconditionalEdge>(16384, node0, node15);
    edge22->setWeight(16512);
    node0->addSuccessor(edge22);
    node15->addPredecessor(edge22);
    auto edge23 = make_shared<UnconditionalEdge>(128, node1, node27);
    edge23->setWeight(128);
    node1->addSuccessor(edge23);
    node27->addPredecessor(edge23);
    auto edge1 = make_shared<UnconditionalEdge>(121, node2, node3);
    edge1->setWeight(121);
    node2->addSuccessor(edge1);
    node3->addPredecessor(edge1);
    auto edge12 = make_shared<UnconditionalEdge>(1, node3, node22);
    edge12->setWeight(122);
    node3->addSuccessor(edge12);
    node22->addPredecessor(edge12);
    auto edge34 = make_shared<UnconditionalEdge>(121, node3, node33);
    edge34->setWeight(122);
    node3->addSuccessor(edge34);
    node33->addPredecessor(edge34);
    auto edge2 = make_shared<UnconditionalEdge>(16384, node4, node5);
    edge2->setWeight(16512);
    node4->addSuccessor(edge2);
    node5->addPredecessor(edge2);
    auto edge59 = make_shared<UnconditionalEdge>(128, node4, node49);
    edge59->setWeight(16512);
    node4->addSuccessor(edge59);
    node49->addPredecessor(edge59);
    auto edge49 = make_shared<UnconditionalEdge>(16384, node5, node40);
    edge49->setWeight(16384);
    node5->addSuccessor(edge49);
    node40->addPredecessor(edge49);
    auto edge3 = make_shared<UnconditionalEdge>(14641, node6, node7);
    edge3->setWeight(14762);
    node6->addSuccessor(edge3);
    node7->addPredecessor(edge3);
    auto edge45 = make_shared<UnconditionalEdge>(121, node6, node12);
    edge45->setWeight(14762);
    node6->addSuccessor(edge45);
    node12->addPredecessor(edge45);
    auto edge41 = make_shared<UnconditionalEdge>(14641, node7, node8);
    edge41->setWeight(14641);
    node7->addSuccessor(edge41);
    node8->addPredecessor(edge41);
    auto edge4 = make_shared<UnconditionalEdge>(29282, node8, node9);
    edge4->setWeight(29282);
    node8->addSuccessor(edge4);
    node9->addPredecessor(edge4);
    auto edge13 = make_shared<UnconditionalEdge>(29282, node9, node23);
    edge13->setWeight(263538);
    node9->addSuccessor(edge13);
    node23->addPredecessor(edge13);
    auto edge52 = make_shared<UnconditionalEdge>(234256, node9, node41);
    edge52->setWeight(263538);
    node9->addSuccessor(edge52);
    node41->addPredecessor(edge52);
    auto edge5 = make_shared<UnconditionalEdge>(1874048, node10, node11);
    edge5->setWeight(1874048);
    node10->addSuccessor(edge5);
    node11->addPredecessor(edge5);
    auto edge11 = make_shared<UnconditionalEdge>(1874048, node11, node21);
    edge11->setWeight(1874048);
    node11->addSuccessor(edge11);
    node21->addPredecessor(edge11);
    auto edge6 = make_shared<UnconditionalEdge>(121, node12, node2);
    edge6->setWeight(121);
    node12->addSuccessor(edge6);
    node2->addPredecessor(edge6);
    auto edge7 = make_shared<UnconditionalEdge>(1, node13, node14);
    edge7->setWeight(1);
    node13->addSuccessor(edge7);
    node14->addPredecessor(edge7);
    auto edge27 = make_shared<UnconditionalEdge>(1, node14, node36);
    edge27->setWeight(129);
    node14->addSuccessor(edge27);
    node36->addPredecessor(edge27);
    auto edge53 = make_shared<UnconditionalEdge>(128, node14, node44);
    edge53->setWeight(129);
    node14->addSuccessor(edge53);
    node44->addPredecessor(edge53);
    auto edge8 = make_shared<UnconditionalEdge>(16384, node15, node16);
    edge8->setWeight(16384);
    node15->addSuccessor(edge8);
    node16->addPredecessor(edge8);
    auto edge54 = make_shared<UnconditionalEdge>(16384, node16, node47);
    edge54->setWeight(16384);
    node16->addSuccessor(edge54);
    node47->addPredecessor(edge54);
    auto edge9 = make_shared<UnconditionalEdge>(121, node17, node18);
    edge9->setWeight(122);
    node17->addSuccessor(edge9);
    node18->addPredecessor(edge9);
    auto edge42 = make_shared<UnconditionalEdge>(1, node17, node46);
    edge42->setWeight(122);
    node17->addSuccessor(edge42);
    node46->addPredecessor(edge42);
    auto edge55 = make_shared<UnconditionalEdge>(121, node18, node25);
    edge55->setWeight(121);
    node18->addSuccessor(edge55);
    node25->addPredecessor(edge55);
    auto edge10 = make_shared<UnconditionalEdge>(14641, node19, node20);
    edge10->setWeight(14641);
    node19->addSuccessor(edge10);
    node20->addPredecessor(edge10);
    auto edge57 = make_shared<UnconditionalEdge>(14641, node20, node25);
    edge57->setWeight(14641);
    node20->addSuccessor(edge57);
    node25->addPredecessor(edge57);
    auto edge21 = make_shared<UnconditionalEdge>(1874048, node21, node10);
    edge21->setWeight(2108304);
    node21->addSuccessor(edge21);
    node10->addPredecessor(edge21);
    auto edge46 = make_shared<UnconditionalEdge>(234256, node21, node42);
    edge46->setWeight(2108304);
    node21->addSuccessor(edge46);
    node42->addPredecessor(edge46);
    auto edge25 = make_shared<UnconditionalEdge>(1, node22, node13);
    edge25->setWeight(1);
    node22->addSuccessor(edge25);
    node13->addPredecessor(edge25);
    auto edge29 = make_shared<UnconditionalEdge>(14641, node23, node19);
    edge29->setWeight(29282);
    node23->addSuccessor(edge29);
    node19->addPredecessor(edge29);
    auto edge14 = make_shared<UnconditionalEdge>(14641, node23, node24);
    edge14->setWeight(29282);
    node23->addSuccessor(edge14);
    node24->addPredecessor(edge14);
    auto edge37 = make_shared<UnconditionalEdge>(14641, node24, node37);
    edge37->setWeight(14641);
    node24->addSuccessor(edge37);
    node37->addPredecessor(edge37);
    auto edge15 = make_shared<UnconditionalEdge>(121, node25, node26);
    edge15->setWeight(14762);
    node25->addSuccessor(edge15);
    node26->addPredecessor(edge15);
    auto edge39 = make_shared<UnconditionalEdge>(14641, node25, node45);
    edge39->setWeight(14762);
    node25->addSuccessor(edge39);
    node45->addPredecessor(edge39);
    auto edge40 = make_shared<UnconditionalEdge>(121, node26, node38);
    edge40->setWeight(121);
    node26->addSuccessor(edge40);
    node38->addPredecessor(edge40);
    auto edge16 = make_shared<UnconditionalEdge>(128, node27, node14);
    edge16->setWeight(128);
    node27->addSuccessor(edge16);
    node14->addPredecessor(edge16);
    auto edge17 = make_shared<UnconditionalEdge>(1, node28, node29);
    edge17->setWeight(1);
    node28->addSuccessor(edge17);
    node29->addPredecessor(edge17);
    auto edge51 = make_shared<UnconditionalEdge>(1, node29, node3);
    edge51->setWeight(1);
    node29->addSuccessor(edge51);
    node3->addPredecessor(edge51);
    auto edge18 = make_shared<UnconditionalEdge>(128, node30, node4);
    edge18->setWeight(128);
    node30->addSuccessor(edge18);
    node4->addPredecessor(edge18);
    auto edge19 = make_shared<UnconditionalEdge>(16384, node31, node32);
    edge19->setWeight(16384);
    node31->addSuccessor(edge19);
    node32->addPredecessor(edge19);
    auto edge58 = make_shared<UnconditionalEdge>(16384, node32, node4);
    edge58->setWeight(16384);
    node32->addSuccessor(edge58);
    node4->addPredecessor(edge58);
    auto edge20 = make_shared<UnconditionalEdge>(121, node33, node6);
    edge20->setWeight(121);
    node33->addSuccessor(edge20);
    node6->addPredecessor(edge20);
    auto edge24 = make_shared<UnconditionalEdge>(1, node34, node17);
    edge24->setWeight(1);
    node34->addSuccessor(edge24);
    node17->addPredecessor(edge24);
    auto edge26 = make_shared<UnconditionalEdge>(1, node35, node28);
    edge26->setWeight(129);
    node35->addSuccessor(edge26);
    node28->addPredecessor(edge26);
    auto edge32 = make_shared<UnconditionalEdge>(128, node35, node30);
    edge32->setWeight(129);
    node35->addSuccessor(edge32);
    node30->addPredecessor(edge32);
    auto edge47 = make_shared<UnconditionalEdge>(1, node36, node34);
    edge47->setWeight(1);
    node36->addSuccessor(edge47);
    node34->addPredecessor(edge47);
    auto edge28 = make_shared<UnconditionalEdge>(14641, node37, node6);
    edge28->setWeight(14641);
    node37->addSuccessor(edge28);
    node6->addPredecessor(edge28);
    auto edge30 = make_shared<UnconditionalEdge>(121, node38, node17);
    edge30->setWeight(121);
    node38->addSuccessor(edge30);
    node17->addPredecessor(edge30);
    auto edge31 = make_shared<UnconditionalEdge>(128, node39, node35);
    edge31->setWeight(128);
    node39->addSuccessor(edge31);
    node35->addPredecessor(edge31);
    auto edge33 = make_shared<UnconditionalEdge>(16384, node40, node31);
    edge33->setWeight(16384);
    node40->addSuccessor(edge33);
    node31->addPredecessor(edge33);
    auto edge35 = make_shared<UnconditionalEdge>(234256, node41, node21);
    edge35->setWeight(234256);
    node41->addSuccessor(edge35);
    node21->addPredecessor(edge35);
    auto edge36 = make_shared<UnconditionalEdge>(234256, node42, node43);
    edge36->setWeight(234256);
    node42->addSuccessor(edge36);
    node43->addPredecessor(edge36);
    auto edge44 = make_shared<UnconditionalEdge>(234256, node43, node9);
    edge44->setWeight(234256);
    node43->addSuccessor(edge44);
    node9->addPredecessor(edge44);
    auto edge38 = make_shared<UnconditionalEdge>(128, node44, node0);
    edge38->setWeight(128);
    node44->addSuccessor(edge38);
    node0->addPredecessor(edge38);
    auto edge56 = make_shared<UnconditionalEdge>(14641, node45, node8);
    edge56->setWeight(14641);
    node45->addSuccessor(edge56);
    node8->addPredecessor(edge56);
    auto edge43 = make_shared<UnconditionalEdge>(16384, node47, node0);
    edge43->setWeight(16384);
    node47->addSuccessor(edge43);
    node0->addPredecessor(edge43);
    auto edge48 = make_shared<UnconditionalEdge>(1, node48, node35);
    edge48->setWeight(1);
    node48->addSuccessor(edge48);
    node35->addPredecessor(edge48);
    auto edge50 = make_shared<UnconditionalEdge>(128, node49, node39);
    edge50->setWeight(128);
    node49->addSuccessor(edge50);
    node39->addPredecessor(edge50);

    Graph subgraph;
    subgraph.nodes.insert(node0);
    subgraph.nodes.insert(node1);
    subgraph.nodes.insert(node2);
    subgraph.nodes.insert(node3);
    subgraph.nodes.insert(node4);
    subgraph.nodes.insert(node5);
    subgraph.nodes.insert(node6);
    subgraph.nodes.insert(node7);
    subgraph.nodes.insert(node8);
    subgraph.nodes.insert(node9);
    subgraph.nodes.insert(node10);
    subgraph.nodes.insert(node11);
    subgraph.nodes.insert(node12);
    subgraph.nodes.insert(node13);
    subgraph.nodes.insert(node14);
    subgraph.nodes.insert(node15);
    subgraph.nodes.insert(node16);
    subgraph.nodes.insert(node17);
    subgraph.nodes.insert(node18);
    subgraph.nodes.insert(node19);
    subgraph.nodes.insert(node20);
    subgraph.nodes.insert(node21);
    subgraph.nodes.insert(node22);
    subgraph.nodes.insert(node23);
    subgraph.nodes.insert(node24);
    subgraph.nodes.insert(node25);
    subgraph.nodes.insert(node26);
    subgraph.nodes.insert(node27);
    subgraph.nodes.insert(node28);
    subgraph.nodes.insert(node29);
    subgraph.nodes.insert(node30);
    subgraph.nodes.insert(node31);
    subgraph.nodes.insert(node32);
    subgraph.nodes.insert(node33);
    subgraph.nodes.insert(node34);
    subgraph.nodes.insert(node35);
    subgraph.nodes.insert(node36);
    subgraph.nodes.insert(node37);
    subgraph.nodes.insert(node38);
    subgraph.nodes.insert(node39);
    subgraph.nodes.insert(node40);
    subgraph.nodes.insert(node41);
    subgraph.nodes.insert(node42);
    subgraph.nodes.insert(node43);
    subgraph.nodes.insert(node44);
    subgraph.nodes.insert(node45);
    subgraph.nodes.insert(node46);
    subgraph.nodes.insert(node47);
    subgraph.nodes.insert(node48);
    subgraph.nodes.insert(node49);
    subgraph.edges.insert(edge0);
    subgraph.edges.insert(edge22);
    subgraph.edges.insert(edge23);
    subgraph.edges.insert(edge1);
    subgraph.edges.insert(edge12);
    subgraph.edges.insert(edge34);
    subgraph.edges.insert(edge2);
    subgraph.edges.insert(edge59);
    subgraph.edges.insert(edge49);
    subgraph.edges.insert(edge3);
    subgraph.edges.insert(edge45);
    subgraph.edges.insert(edge41);
    subgraph.edges.insert(edge4);
    subgraph.edges.insert(edge13);
    subgraph.edges.insert(edge52);
    subgraph.edges.insert(edge5);
    subgraph.edges.insert(edge11);
    subgraph.edges.insert(edge6);
    subgraph.edges.insert(edge7);
    subgraph.edges.insert(edge27);
    subgraph.edges.insert(edge53);
    subgraph.edges.insert(edge8);
    subgraph.edges.insert(edge54);
    subgraph.edges.insert(edge9);
    subgraph.edges.insert(edge42);
    subgraph.edges.insert(edge55);
    subgraph.edges.insert(edge10);
    subgraph.edges.insert(edge57);
    subgraph.edges.insert(edge21);
    subgraph.edges.insert(edge46);
    subgraph.edges.insert(edge25);
    subgraph.edges.insert(edge29);
    subgraph.edges.insert(edge14);
    subgraph.edges.insert(edge37);
    subgraph.edges.insert(edge15);
    subgraph.edges.insert(edge39);
    subgraph.edges.insert(edge40);
    subgraph.edges.insert(edge16);
    subgraph.edges.insert(edge17);
    subgraph.edges.insert(edge51);
    subgraph.edges.insert(edge18);
    subgraph.edges.insert(edge19);
    subgraph.edges.insert(edge58);
    subgraph.edges.insert(edge20);
    subgraph.edges.insert(edge24);
    subgraph.edges.insert(edge26);
    subgraph.edges.insert(edge32);
    subgraph.edges.insert(edge47);
    subgraph.edges.insert(edge28);
    subgraph.edges.insert(edge30);
    subgraph.edges.insert(edge31);
    subgraph.edges.insert(edge33);
    subgraph.edges.insert(edge35);
    subgraph.edges.insert(edge36);
    subgraph.edges.insert(edge44);
    subgraph.edges.insert(edge38);
    subgraph.edges.insert(edge56);
    subgraph.edges.insert(edge43);
    subgraph.edges.insert(edge48);
    subgraph.edges.insert(edge50);
    ofstream Original("SharedFunctionGraph.dot");
    auto OriginalGraph = GenerateDot(subgraph.nodes, std::set<shared_ptr<MLCycle>, KCompare>());
    Original << OriginalGraph << "\n";
    Original.close();
    return subgraph;
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
void Checks(Graph &original, Graph &transformed, string step)
{
    // 1. the graphs should not be empty
    if (transformed.nodes.empty() && !original.nodes.empty())
    {
        throw AtlasException(step + ": Transformed graph is empty!");
    }
    // 2. all preds and succs should be present
    for (const auto node : transformed.nodes)
    {
        for (auto pred : node->getPredecessors())
        {
            if (transformed.edges.find(pred) == transformed.edges.end())
            {
                throw AtlasException(step + ": Predecessor edge missing!");
            }
            if (transformed.nodes.find(pred->src) == transformed.nodes.end())
            {
                throw AtlasException(step + ": Predecessor source missing!");
            }
            if (transformed.nodes.find(pred->snk) == transformed.nodes.end())
            {
                throw AtlasException(step + ": Predecessor sink missing!");
            }
        }
        for (auto succ : node->getSuccessors())
        {
            if (transformed.edges.find(succ) == transformed.edges.end())
            {
                throw AtlasException(step + ": Successor missing!");
            }
            if (transformed.nodes.find(succ->src) == transformed.nodes.end())
            {
                throw AtlasException(step + ": Successor source missing!");
            }
            if (transformed.nodes.find(succ->snk) == transformed.nodes.end())
            {
                throw AtlasException(step + ": Successor sink missing!");
            }
        }
    }
    // 3. The graph should be one complete piece
    // we check this by finding a "start" and "end" node (nodes with no preds, succs, respectively) and if there are more than one of these... wrong
    bool foundStart = false;
    bool foundEnd = false;
    for (auto node : transformed.nodes)
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
    for (auto node : original.nodes)
    {
        if (node->getSuccessors().empty())
        {
            continue;
        }
        double sum = 0.0;
        for (auto succ : node->getSuccessors())
        {
            sum += succ->prob;
        }
        if (sum < 0.9999 || sum > 1.0001)
        {
            throw AtlasException(step + ": Outgoing edges do not sum to 1!");
        }
    }
}

void ReverseTransformCheck(Graph original, Graph transformed, string step)
{
    reverseTransform(transformed);
    for (auto node : transformed.nodes)
    {
        if (original.nodes.find(node->NID) == original.nodes.end())
        {
            throw AtlasException(step + ": Node in transformed graph not found in original!");
        }
        auto origNode = *original.nodes.find(node->NID);
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
    for (auto node : original.nodes)
    {
        if (original.nodes.find(node->NID) == original.nodes.end())
        {
            throw AtlasException(step + ": Node in original graph not found in transformed!");
        }
        auto transformedNode = *original.nodes.find(node->NID);
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

uint8_t RunTest(Graph original, llvm::Module *sourceBitcode, map<int64_t, llvm::BasicBlock *> &IDToBlock, map<int64_t, vector<int64_t>> &blockCallers, llvm::CallGraph &CG)
{
    auto transformed = original;
    auto oldSize = transformed.nodes.size();
    try
    {
        while (true)
        {
            size_t graphSize = transformed.nodes.size();
            // Inline all the shared functions in the graph
            string DotString = "# SharedFunction\n\n# Subgraph\n";
            DotString += "\n# Old Graph\n";
            DotString += GenerateDot(transformed.nodes, std::set<shared_ptr<MLCycle>, KCompare>());
            VirtualizeSharedFunctions(transformed, IDToBlock, CG);
            if (graphSize != transformed.nodes.size())
            {
                DotString += "\n# New Graph\n";
                ofstream LastTransform("LastTransform.dot");
                DotString += GenerateDot(transformed.nodes, std::set<shared_ptr<MLCycle>, KCompare>());
                LastTransform << DotString << "\n";
                LastTransform.close();
                Checks(original, transformed, "Trivial");
                graphSize = transformed.nodes.size();
            }
            auto tmpNodes = vector<shared_ptr<ControlNode>>(transformed.nodes.begin(), transformed.nodes.end());
            for (auto source : tmpNodes)
            {
                if (transformed.nodes.find(source) == transformed.nodes.end())
                {
                    continue;
                }
                set<shared_ptr<ControlNode>, p_GNCompare> covered;
                deque<shared_ptr<ControlNode>> Q;
                Q.push_front(source);
                covered.insert(source);
                while (!Q.empty())
                {
                    auto sink = Q.front();
                    // combine all trivial node merges
                    auto sub = TrivialTransforms(source);
                    if (!sub.empty())
                    {
                        ofstream LastTransform("LastTransform.dot");
                        string DotString = "# Trivial Transform\n\n# Subgraph\n";
                        DotString += GenerateDot(sub, std::set<shared_ptr<MLCycle>, KCompare>());
                        DotString += "\n# Old Graph\n";
                        DotString += GenerateDot(transformed.nodes, std::set<shared_ptr<MLCycle>, KCompare>());
                        DotString += "\n# New Graph\n";
                        auto VN = make_shared<VirtualNode>();
                        VirtualizeSubgraph(transformed, VN, sub);
                        DotString += GenerateDot(transformed.nodes, std::set<shared_ptr<MLCycle>, KCompare>());
                        LastTransform << DotString << "\n";
                        LastTransform.close();
                        Checks(original, transformed, "Trivial");
                        ReverseTransformCheck(original, transformed, "Trivial");
                        break;
                    }
                    // Next transform, find conditional branches and turn them into select statements
                    // In other words, find subgraphs of nodes that have a common entrance and exit, flow from one end to the other, and combine them into a single node
                    sub = BranchToSelectTransforms(transformed, source);
                    if (!sub.empty())
                    {
                        ofstream LastTransform("LastTransform.dot");
                        string DotString = "# BranchToSelect\n\n# Subgraph\n";
                        DotString += GenerateDot(sub, std::set<shared_ptr<MLCycle>, KCompare>());
                        DotString += "\n# Old Graph\n";
                        DotString += GenerateDot(transformed.nodes, std::set<shared_ptr<MLCycle>, KCompare>());
                        DotString += "\n# New Graph\n";
                        auto VN = make_shared<VirtualNode>();
                        VirtualizeSubgraph(transformed, VN, sub);
                        DotString += GenerateDot(transformed.nodes, std::set<shared_ptr<MLCycle>, KCompare>());
                        LastTransform << DotString << "\n";
                        LastTransform.close();
                        Checks(original, transformed, "BranchToSelect");
                        ReverseTransformCheck(original, transformed, "BranchToSelect");
                        break;
                    }
                    // Transform bottlenecks to avoid multiple entrance/multiple exit kernels
                    sub = FanInFanOutTransform(transformed, source, sink);
                    if (!sub.empty())
                    {
                        ofstream LastTransform("LastTransform.dot");
                        string DotString = "# FanInFanOut\n\n# Subgraph\n";
                        DotString += GenerateDot(sub, std::set<shared_ptr<MLCycle>, KCompare>());
                        DotString += "\n# Old Graph\n";
                        DotString += GenerateDot(transformed.nodes, std::set<shared_ptr<MLCycle>, KCompare>());
                        DotString += "\n# New Graph\n";
                        auto VN = make_shared<VirtualNode>();
                        VirtualizeSubgraph(transformed, VN, sub);
                        DotString += GenerateDot(transformed.nodes, std::set<shared_ptr<MLCycle>, KCompare>());
                        LastTransform << DotString << "\n";
                        LastTransform.close();
                        Checks(original, transformed, "FanInFanOut");
                        ReverseTransformCheck(original, transformed, "FanInFanOut");
                        break;
                    }
                    // Finally, merge all valid forks in the program to a single node
                    sub = MergeForks(transformed, source);
                    if (!sub.empty())
                    {
                        ofstream LastTransform("LastTransform.dot");
                        string DotString = "# MergeFork\n\n# Subgraph\n";
                        DotString += GenerateDot(sub, std::set<shared_ptr<MLCycle>, KCompare>());
                        DotString += "\n# Old Graph\n";
                        DotString += GenerateDot(transformed.nodes, std::set<shared_ptr<MLCycle>, KCompare>());
                        DotString += "\n# New Graph\n";
                        auto VN = make_shared<VirtualNode>();
                        VirtualizeSubgraph(transformed, VN, sub);
                        DotString += GenerateDot(transformed.nodes, std::set<shared_ptr<MLCycle>, KCompare>());
                        LastTransform << DotString << "\n";
                        LastTransform.close();
                        Checks(original, transformed, "MergeFork");
                        ReverseTransformCheck(original, transformed, "MergeFork");
                        break;
                    }
                    // search for new nodes to push into the Q
                    for (auto succ : Q.front()->getSuccessors())
                    {
                        if (covered.find(succ->snk) == covered.end())
                        {
                            Q.push_back(succ->snk);
                            covered.insert(succ->snk);
                        }
                    }
                    Q.pop_front();
                } // while( !Q.empty() )

            } // for( auto source : transformed.nodes )
            if (graphSize == transformed.nodes.size())
            {
                break;
            }
        } // while( true )
        return EXIT_SUCCESS;
    }
    catch (AtlasException &e)
    {
        spdlog::critical(e.what());
        return EXIT_FAILURE;
    }
}

int main()
{
    auto BF = string("../../build/Tests/SharedFunction/BlockInfo.json");
    auto BC = string("../../build/Tests/SharedFunction/SharedFunction");
    auto IP = string("../../build/Tests/SharedFunction/markov.bin");
    auto blockCallers = ReadBlockInfo(BF);
    auto blockLabels = ReadBlockLabels(BF);
    auto SourceBitcode = ReadBitcode(BC);
    if (SourceBitcode == nullptr)
    {
        return EXIT_FAILURE;
    }
    // Annotate its bitcodes and values
    //CleanModule(SourceBitcode.get());
    Format(SourceBitcode.get());
    // construct its callgraph
    map<int64_t, BasicBlock *> IDToBlock;
    map<int64_t, Value *> IDToValue;
    InitializeIDMaps(SourceBitcode.get(), IDToBlock, IDToValue);

    // Construct bitcode CallGraph
    map<BasicBlock *, Function *> BlockToFPtr;
    auto CG = getCallGraph(SourceBitcode.get(), blockCallers, BlockToFPtr, IDToBlock);

    // Read input profile
    Graph original;
    try
    {
        auto err = BuildCFG(original, IP, false);
        if (err)
        {
            throw AtlasException("Failed to read input profile file!");
        }
        if (original.nodes.empty())
        {
            throw AtlasException("No nodes could be read from the input profile!");
        }
        UpgradeEdges(SourceBitcode.get(), original, blockCallers, IDToBlock);
    }
    catch (AtlasException &e)
    {
        spdlog::critical(e.what());
        return EXIT_FAILURE;
    }

    spdlog::info("Running SharedFunction test");
    if (RunTest(original, SourceBitcode.get(), IDToBlock, blockCallers, CG))
    {
        return EXIT_FAILURE;
    }

    spdlog::info("FunctionInline pass all tests!");
    return EXIT_SUCCESS;
}