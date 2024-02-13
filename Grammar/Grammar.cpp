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
cl::opt<string> OutputFile("o", cl::desc("Specify output json filename"), cl::value_desc("json filename"));

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
    auto tasks = getTasks(instanceJson, kernelJson, IDToBlock);
    // color the nodes of the graph
    colorNodes(tasks);
    // print for everyone to see
    PrintDFGs(tasks);
    // interpret the tasks in the DFG
    Process(tasks);

    // output json file with special instruction information
    uint64_t staticInstructions  = 0;
    uint64_t dynamicInstructions = 0;
    uint64_t kernelInstructions  = 0;
    uint64_t labeledInstructions = 0;
    for( auto f = SourceBitcode->begin(); f != SourceBitcode->end(); f++ )
    {
        for( auto b = f->begin(); b != f->end(); b++ )
        {
            uint64_t blockInstCount = 0;
            for( auto i = b->begin(); i != b->end(); i++ )
            {
                blockInstCount++;
            }
            staticInstructions += blockInstCount;
            if( BBCBMap.contains(llvm::cast<BasicBlock>(b)) )
            {
                dynamicInstructions += blockInstCount;
            }
        }
    }
    for( const auto& t : tasks )
    {
        for( const auto& c : t->getCycles() )
        {
            for( const auto& b : c->getBody() )
            {
                for( const auto& i : b->getInstructions() )
                {
                    kernelInstructions++;
                    if( i->isFunction() || i->isMemory() || i->isState() )
                    {
                        labeledInstructions++;
                    }
                }
            }
        }
    }
    nlohmann::json output;
    output["Statistics"]["StaticInstCount"]  = staticInstructions;
    output["Statistics"]["DynamicInstCount"] = dynamicInstructions;
    output["Statistics"]["KernelInstCount"]  = kernelInstructions;
    output["Statistics"]["LabeledInstCount"] = labeledInstructions;
    
    // kernel function histogram
    map<string, uint64_t> hist;
    for( const auto& t : tasks )
    {
        for( const auto& c : t->getCycles() )
        {
            for( const auto& b : c->getBody() )
            {
                for( const auto& i : b->getInstructions() )
                {
                    if( i->isFunction() )
                    {
                        try
                        {
                            hist[Cyclebite::Graph::OperationToString.at(i->getOp())]++;
                        }
                        catch( exception& e )
                        {
                            spdlog::critical("OpToString map failed on the following instruction: ");
                            Cyclebite::Util::PrintVal(i->getInst());
                        }
                    }
                }
            }
        }
    }
    output["Statistics"]["FunctionHistogram"] = hist;

    ofstream oStream(OutputFile);
    oStream << setw(4) << output;
    oStream.close();

    return 0;
}