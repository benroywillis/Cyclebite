//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "Graph/inc/IO.h"
#include "Util/Format.h"
#include "Util/IO.h"
#include "ControlGraph.h"
#include "DataGraph.h"
#include "CallGraph.h"
#include "IO.h"
#include <fstream>
#include <iostream>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/SourceMgr.h>
#include <string>

using namespace std;
using namespace llvm;
using namespace Cyclebite::Graph;

cl::opt<string> KernelFileName("k", cl::desc("Specify input kernel json filename"), cl::value_desc("kernel filename"));
cl::opt<string> BitcodeFileName("b", cl::desc("Specify input bitcode filename"), cl::value_desc("bitcode filename"));
cl::opt<string> BlockInfoFilename("bi", cl::desc("Specify input BlockInfo filename"), cl::value_desc("BlockInfo filename"));
cl::opt<string> LoopFileName("l", cl::desc("Specify Loopinfo.json file"), cl::value_desc(".json filename"), cl::init("Loopinfo.json"));
cl::opt<string> ProfileFileName("p", cl::desc("Specify input profile filename"), cl::value_desc("profile filename"));
cl::opt<string> OutputFileName("o", cl::desc("Specify output dot filename"), cl::value_desc("dot filename"));

int main(int argc, char *argv[])
{
    cl::ParseCommandLineOptions(argc, argv);
    // load dynamic source code information
    Cyclebite::Graph::ReadBlockInfo(BlockInfoFilename);
    // load bitcode
    LLVMContext context;
    SMDiagnostic smerror;
    auto SourceBitcode = parseIRFile(BitcodeFileName, smerror, context);
    Cyclebite::Util::Format(*SourceBitcode);
    // construct its callgraph
    Cyclebite::Graph::InitializeIDMaps(SourceBitcode.get());
    // construct static call graph from the input bitcode
    llvm::CallGraph staticCG(*SourceBitcode);
    // construct program control graph and call graph
    ControlGraph cg;
    Cyclebite::Graph::CallGraph dynamicCG;
    getDynamicInformation(cg, dynamicCG, ProfileFileName, SourceBitcode, staticCG, blockCallers, threadStarts, Cyclebite::Graph::IDToBlock, false );

    // construct block ID to node ID mapping
    map<int64_t, shared_ptr<ControlNode>> blockToNode;
    for (const auto &it : NIDMap)
    {
        for (const auto &block : it.first)
        {
            blockToNode[block] = cg.getNode(it.second);
        }
    }
    // loop information
    ifstream loopfile;
    nlohmann::json loopjson;
    try
    {
        loopfile.open(LoopFileName);
        loopfile >> loopjson;
        loopfile.close();
    }
    catch (std::exception &e)
    {
        spdlog::critical("Couldn't open loop file " + string(LoopFileName) + ": " + string(e.what()));
    }

    /* this section constructs the data flow and ControlBlock */
    // BBsubgraphs of the program
    set<shared_ptr<ControlBlock>, p_GNCompare> programFlow;
    // data flow of the program
    DataGraph dGraph;
    BuildDFG( programFlow, dGraph, SourceBitcode, dynamicCG, blockToNode, Cyclebite::Graph::IDToBlock);

    ofstream DFGDot("DFG.dot");
    auto dataGraph = GenerateDataDot(dGraph.getDataNodes());
    DFGDot << dataGraph << "\n";
    DFGDot.close();

    ofstream BBSubgraphDot("ControlBlock.dot");
    auto BBSDot = GenerateBBSubgraphDot(programFlow);
    BBSubgraphDot << BBSDot << "\n";
    BBSubgraphDot.close();
}