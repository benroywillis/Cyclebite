// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
#include "Util/Format.h"
#include "Util/IO.h"
#include "ControlGraph.h"
#include "Graph.h"
#include "IO.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/SourceMgr.h>
#include <string>

using namespace std;
using namespace llvm;
using namespace Cyclebite::Graph;
using json = nlohmann::json;

cl::opt<string> BitcodeFileName("b", cl::desc("Specify input bitcode filename"), cl::value_desc("bitcode filename"));
cl::opt<string> ProfileFileName("p", cl::desc("Specify input profile filename"), cl::value_desc("profile filename"));
cl::opt<string> DotFileName("d", cl::desc("Specify output dotfile name"), cl::value_desc("dot filename"));
cl::opt<string> OutputFileName("o", cl::desc("Specify output .cpp filename"), cl::value_desc("source file name"));

// This program turns a binary profile file into a source code (c++) file that enumerates the entire graph
int main(int argc, char *argv[])
{
    cl::ParseCommandLineOptions(argc, argv);
    // load dynamic source code information
    //auto blockCallers = ReadBlockInfo(BlockInfoFilename);
    //auto blockLabels = ReadBlockLabels(BlockInfoFilename);
    // load bitcode
    LLVMContext context;
    SMDiagnostic smerror;
    auto SourceBitcode = parseIRFile(BitcodeFileName, smerror, context);
    Cyclebite::Util::Format(*SourceBitcode);
    // construct its callgraph
    map<int64_t, BasicBlock *> IDToBlock;
    map<int64_t, Value *> IDToValue;
    Cyclebite::Util::InitializeIDMaps(SourceBitcode.get(), IDToBlock, IDToValue);
    // Construct bitcode CallGraph
    //map<BasicBlock *, Function *> BlockToFPtr;
    //auto CG = getCallGraph(SourceBitcode.get(), blockCallers, BlockToFPtr, IDToBlock);
    // get the input profile
    Graph graph;
    try
    {
        auto err = BuildCFG(graph, ProfileFileName, false);
        if (err)
        {
            throw CyclebiteException("Failed to read input profile file!");
        }
        if (graph.empty())
        {
            throw CyclebiteException("No nodes could be read from the input profile!");
        }
    }
    catch (CyclebiteException &e)
    {
        spdlog::critical(e.what());
        return EXIT_FAILURE;
    }
    // print the dot file of the graph
    auto dot = GenerateDot(graph);
    auto dotfile = ofstream(DotFileName);
    dotfile << setw(2) << dot;
    dotfile.close();

    string sourceFile = "Graph GenerateSubgraph() {\n";
    for (auto node : graph.nodes())
    {
        auto nodeName = "node" + to_string(node->NID);
        sourceFile += "\tauto " + nodeName + " = make_shared<ControlNode>(" + to_string(node->NID) + ");\n";
    }
    sourceFile += "\n";
    for (auto edge : graph.edges())
    {
        auto edgeName = "edge" + to_string(edge->EID);
        auto srcNode = "node" + to_string(edge->getSrc()->NID);
        auto snkNode = "node" + to_string(edge->getSnk()->NID);
        uint64_t weight = 0;
        for (auto succ : edge->getSrc()->getSuccessors())
        {
            if( auto ue = dynamic_pointer_cast<UnconditionalEdge>(succ) )
            {
                weight += ue->getFreq();
            }
        }
        if( auto ue = dynamic_pointer_cast<UnconditionalEdge>(edge) )
        {
            sourceFile += "\tauto " + edgeName + " = make_shared<UnconditionalEdge>(" + to_string(ue->getFreq()) + ", " + srcNode + ", " + snkNode + ");\n";
        }
        sourceFile += "\t" + edgeName + "->setWeight(" + to_string(weight) + ");\n";
        sourceFile += "\t" + srcNode + "->addSuccessor(" + edgeName + ");\n";
        sourceFile += "\t" + snkNode + "->addPredecessor(" + edgeName + ");\n";
    }
    sourceFile += "\n\tGraph subgraph;\n";
    for (auto node : graph.nodes())
    {
        auto newNode = "node" + to_string(node->NID);
        sourceFile += "\tsubgraph.nodes.insert(" + newNode + ");\n";
    }
    for (auto edge : graph.edges())
    {
        auto newEdge = "edge" + to_string(edge->EID);
        sourceFile += "\tsubgraph.edges.insert(" + newEdge + ");\n";
    }
    sourceFile += "\treturn subgraph;\n}";

    auto out = ofstream(OutputFileName);
    out << setw(2) << sourceFile;
    out.close();
    return 0;
}