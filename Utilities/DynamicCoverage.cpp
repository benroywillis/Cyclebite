//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "Util/Format.h"
#include "Util/IO.h"
#include "CallGraph.h"
#include "ControlGraph.h"
#include "IO.h"
#include "Transforms.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <llvm/Support/CommandLine.h>
#include <queue>

using namespace llvm;
using namespace std;
using namespace Cyclebite::Graph;
using json = nlohmann::json;

cl::opt<string> InputFilename("p", cl::desc("Specify profile file"), cl::value_desc(".bin filename"), cl::Required);
cl::opt<string> BitcodeFileName("b", cl::desc("Specify bitcode file"), cl::value_desc(".bc filename"), cl::Required);
cl::opt<string> BlockInfoFilename("bi", cl::desc("Specify BlockInfo.json file"), cl::value_desc(".json filename"), cl::Required);
cl::opt<string> DotFile("o", cl::desc("Specify output dotfile name"), cl::value_desc("dot file"));

extern uint32_t markovOrder;
// maps a block ID list (that represents the markov chain state, which could have arbitrary order number) to an ID for that node
//map<vector<uint32_t>, uint64_t> NIDMap;

int main(int argc, char *argv[])
{
    cl::ParseCommandLineOptions(argc, argv);
    auto blockCallers = ReadBlockInfo(BlockInfoFilename);
    auto blockLabels = ReadBlockLabels(BlockInfoFilename);
    auto SourceBitcode = ReadBitcode(BitcodeFileName);
    if (SourceBitcode == nullptr)
    {
        return EXIT_FAILURE;
    }
    // Annotate its bitcodes and values
    Cyclebite::Util::Format(*SourceBitcode);
    // construct its callgraph
    map<int64_t, BasicBlock *> IDToBlock;
    map<int64_t, Value *> IDToValue;
    Cyclebite::Util::InitializeIDMaps(SourceBitcode.get(), IDToBlock, IDToValue);

    // Set of nodes that constitute the entire graph
    ControlGraph graph;
    // maps each block ID to its frequency count (used for performance intrinsics calculations later, must happen before transforms because ControlNodes are 1:1 with blocks)
    map<int64_t, uint64_t> blockFrequencies;

    try
    {
        auto err = BuildCFG(graph, InputFilename, false);
        if (err)
        {
            throw CyclebiteException("Failed to read input profile file!");
        }
        if (graph.empty())
        {
            throw CyclebiteException("No nodes could be read from the input profile!");
        }
        // accumulate block frequencies
        for (const auto &block : graph.nodes())
        {
            // we sum along the columns (the probabilities of going to the current node), so we use the edge weight coming from each predecessor to this node
            for (const auto &pred : block->getPredecessors())
            {
                if( auto ue = dynamic_pointer_cast<UnconditionalEdge>(pred) )
                {
                    // retrieve the edge frequency and accumulate it to this node
                    blockFrequencies[(int64_t)ue->getSnk()->NID] += ue->getFreq();
                }
            }
        }
    }
    catch (CyclebiteException &e)
    {
        spdlog::critical(e.what());
        return EXIT_FAILURE;
    }

    // Construct bitcode CallGraph
    map<BasicBlock *, Function *> BlockToFPtr;
    auto CG = getDynamicCallGraph(SourceBitcode.get(), graph, blockCallers, IDToBlock);

    /*auto transformedGraph = Cyclebite::Graph::ReduceMO(graph.nodes, (int)markovOrder, 1);
    try
    {
        TrivialTransforms(transformedGraph);
    }
    catch (CyclebiteException &e)
    {
        spdlog::error("Exception while conducting trivial transforms on reduced markov graph:");
        spdlog::error(e.what());
    }*/
    // transform graph in an iterative manner until the size of the graph doesn't change
    ApplyCFGTransforms(graph, CG);

    auto staticNodes = GenerateStaticCFG(SourceBitcode.get());
#ifdef DEBUG
    ofstream debugStream("StaticControlGraph.dot");
    auto staticGraph = GenerateDot(graph);
    debugStream << staticGraph << "\n";
    debugStream.close();
#endif
    GenerateDynamicCoverage(graph.getControlNodes(), staticNodes.getControlNodes());
    return 0;
}