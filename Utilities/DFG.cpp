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
    auto blockCallers = ReadBlockInfo(BlockInfoFilename);
    auto blockLabels = ReadBlockLabels(BlockInfoFilename);
    auto threadStarts = ReadThreadStarts(BlockInfoFilename);
    // load bitcode
    LLVMContext context;
    SMDiagnostic smerror;
    auto SourceBitcode = parseIRFile(BitcodeFileName, smerror, context);
    Cyclebite::Util::Format(*SourceBitcode);
    // construct its callgraph
    map<int64_t, BasicBlock *> IDToBlock;
    map<int64_t, Value *> IDToValue;
    Cyclebite::Util::InitializeIDMaps(SourceBitcode.get(), IDToBlock, IDToValue);
    // construct static call graph from the input bitcode
    llvm::CallGraph staticCG(*SourceBitcode);
    // construct program control graph and call graph
    ControlGraph cg;
    Cyclebite::Graph::CallGraph dynamicCG;
    getDynamicInformation(cg, dynamicCG, ProfileFileName, SourceBitcode, staticCG, blockCallers, threadStarts, IDToBlock, false );

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
    // build map of special instructions
    map<string, set<int64_t>> specials;
    for (int i = 0; i < (int)loopjson["Loops"].size(); i++)
    {
        if (loopjson["Loops"][(uint64_t)i].find("IV") != loopjson["Loops"][(uint64_t)i].end())
        {
            for (auto IV : loopjson["Loops"][(uint64_t)i]["IV"].get<vector<int64_t>>())
            {
                specials["IV"].insert(IV);
            }
        }
        if (loopjson["Loops"][(uint64_t)i].find("BasePointers") != loopjson["Loops"][(uint64_t)i].end())
        {
            for (auto base : loopjson["Loops"][(uint64_t)i]["BasePointers"].get<vector<int64_t>>())
            {
                specials["BP"].insert(base);
            }
        }
        if (loopjson["Loops"][(uint64_t)i].find("Functions") != loopjson["Loops"][(uint64_t)i].end())
        {
            for (auto base : loopjson["Loops"][(uint64_t)i]["Functions"].get<vector<int64_t>>())
            {
                specials["KF"].insert(base);
            }
        }
    }

    /* this section constructs the data flow and ControlBlock */
    // BBsubgraphs of the program
    set<shared_ptr<ControlBlock>, p_GNCompare> programFlow;
    // data flow of the program
    DataGraph dGraph;
    if (BuildDFG(SourceBitcode.get(), dynamicCG, blockToNode, programFlow, dGraph, specials, IDToBlock))
    {
        throw CyclebiteException("Failed to build DFG!");
    }

    ofstream DFGDot("DFG.dot");
    auto dataGraph = GenerateDataDot(dGraph.getDataNodes());
    DFGDot << dataGraph << "\n";
    DFGDot.close();

    ofstream BBSubgraphDot("ControlBlock.dot");
    auto BBSDot = GenerateBBSubgraphDot(programFlow);
    BBSubgraphDot << BBSDot << "\n";
    BBSubgraphDot.close();
}