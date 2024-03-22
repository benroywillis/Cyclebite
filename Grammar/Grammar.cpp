//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "Util/Format.h"
#include "Util/Print.h"
#include "Util/IO.h"
#include "ControlBlock.h"
#include "ControlGraph.h"
#include "Graph/inc/IO.h"
#include "CallGraph.h"
#include "DataGraph.h"
#include "Categorize.h"
#include "Process.h"
#include "Task.h"
#include "IO.h"
#include "Export.h"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <llvm/IRReader/IRReader.h>
#include <nlohmann/json.hpp>

using namespace Cyclebite::Graph;
using namespace Cyclebite::Grammar;
using namespace llvm;
using namespace std;

cl::opt<string> InstanceFile("i", cl::desc("Specify input instance json filename"), cl::value_desc("instance filename"));
cl::opt<string> KernelFile("k", cl::desc("Specify input kernel json filename"), cl::value_desc("kernel filename"));
cl::opt<string> BitcodeFileName("b", cl::desc("Specify input bitcode filename"), cl::value_desc("bitcode filename"));
cl::opt<string> BlockInfoFilename("bi", cl::desc("Specify input BlockInfo filename"), cl::value_desc("BlockInfo filename"));
cl::opt<string> ProfileFileName("p", cl::desc("Specify input profile filename"), cl::value_desc("profile filename"));
cl::opt<bool> LabelTasks("label", cl::desc("Enable task label assignment"), cl::value_desc("Enable task labels"), cl::init(true));
cl::opt<bool> OutputOMP("omp", cl::desc("Enable OMP code generation. Each source file (including headers) used in the input application will be annotated with OMP pragmas where parallel tasks were found."), cl::value_desc("Enable task labels"), cl::init(true));
cl::opt<bool> OutputHalide("halide", cl::desc("Enable automatic Halide generation"), cl::value_desc("Enable halide generation"), cl::init(true));
cl::opt<string> OutputFile("o", cl::desc("Specify output json filename"), cl::value_desc("json filename"), cl::init("KernelGrammar.json"));

int main(int argc, char *argv[])
{
    cl::ParseCommandLineOptions(argc, argv);
    // load dynamic source code information
    ReadBlockInfo(BlockInfoFilename);
    // load bitcode
    LLVMContext context;
    SMDiagnostic smerror;
    auto SourceBitcode = parseIRFile(BitcodeFileName, smerror, context);
    Cyclebite::Util::Format(*SourceBitcode, false);
    // construct its callgraph

    InitializeIDMaps(SourceBitcode.get());
    // build IR to source maps (must be done after ID maps are initialized)
    InitSourceMaps(SourceBitcode);

    // construct static call graph from the input bitcode
    llvm::CallGraph staticCG(*SourceBitcode);
    // construct program control graph and call graph
    ControlGraph cg;
    Cyclebite::Graph::CallGraph dynamicCG;
    getDynamicInformation( cg, dynamicCG, ProfileFileName, SourceBitcode, staticCG, blockCallers, threadStarts, IDToBlock, false );
    // construct block ID to node ID mapping
    map<int64_t, shared_ptr<ControlNode>> blockToNode;
    for (const auto &it : NIDMap)
    {
        for (const auto &block : it.first)
        {
            blockToNode[block] = cg.getNode(it.second);
        }
    }

    /* this section constructs the data flow and shared_ptr<ControlBlock> */
    ifstream kernelFile(KernelFile);
    nlohmann::json kernelJson;
    kernelFile >> kernelJson;
    kernelFile.close();
    ifstream instanceFile(InstanceFile);
    nlohmann::json instanceJson;
    instanceFile >> instanceJson;
    instanceFile.close();
    // BBsubgraphs of the program
    set<shared_ptr<ControlBlock>, p_GNCompare> programFlow;
    // data flow of the program
    DataGraph dGraph;
    BuildDFG(programFlow, dGraph, SourceBitcode, dynamicCG, blockToNode, IDToBlock);
    // takes the information from EP about which loads and stores touch significant memory chunks and injects that info into the DFG
    InjectSignificantMemoryInstructions(instanceJson, IDToValue);
    BuildMemoryInstructionMappings( instanceJson, IDToValue );
    auto tasks = getTasks(instanceJson, kernelJson, IDToBlock);
    // color the nodes of the graph
    colorNodes(tasks);
    // print for everyone to see
    PrintDFGs(tasks);
    // interpret the tasks in the DFG
    auto taskToExpr = Process(tasks);
    // finally, export the processed tasks
    Export(taskToExpr, OutputFile, LabelTasks, OutputOMP, OutputHalide);
    // output json file with special instruction information
    OutputJson(SourceBitcode, tasks, OutputFile);
    return 0;
}