#include "Util/Format.h"
#include "Util/Print.h"
#include "Util/IO.h"
#include "ControlBlock.h"
#include "ControlGraph.h"
#include "IO.h"
#include "CallGraph.h"
#include "DataGraph.h"
#include "Categorize.h"
#include "Process.h"
#include "Task.h"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/CommandLine.h>
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
    // load input kernels
    // the instance file only contains the parent-most kernel, but we are interested in the entire hierarchy
    // thus we will combine all kernels together to form the entire hierarchy
    ifstream kernelFile(KernelFile);
    nlohmann::json kernelJson;
    kernelFile >> kernelJson;
    kernelFile.close();
    map<string, vector<int64_t>> kernels;
    for (auto &[k, l] : kernelJson["Kernels"].items())
    {
        string index = k;
        nlohmann::json kernel = l["Blocks"];
        if( kernel.size() )
        {
            kernels[index] = kernel.get<vector<int64_t>>();
        }
        else
        {
            kernels[index] = vector<int64_t>();
        }
    }
    ifstream instanceFile(InstanceFile);
    nlohmann::json instanceJson;
    instanceFile >> instanceJson;
    instanceFile.close();
    map<string, vector<int64_t>> tasks;
    for (auto &[i, l] : instanceJson["Kernels"].items())
    {
        string index = i;
        nlohmann::json instance = l["Blocks"];
        set<int64_t> blocks;
        for( const auto& id : instance.get<vector<int64_t>>() )
        {
            blocks.insert(id);
        }
        // group in its children... which must be done recursively
        deque<string> Q;
        set<string> covered;
        for( const auto& c : l["Children"] )
        {
            string childID = to_string(c.get<int64_t>());
            Q.push_front(childID);
            covered.insert(childID);
        }
        while( !Q.empty() )
        {
            nlohmann::json ck = kernelJson["Kernels"][Q.front()];
            for( const auto& id : ck["Blocks"].get<vector<int64_t>>() )
            {
                blocks.insert(id);
            }
            for( const auto& c : ck["Children"] )
            {
                string id = to_string(c.get<int64_t>());
                if( covered.find( id ) == covered.end() )
                {
                    Q.push_back(id);
                    covered.insert(id);
                }
            }
            Q.pop_front();
        }
        tasks[index] = vector<int64_t>(blocks.begin(), blocks.end());
    }
    // load dynamic source code information
    auto blockCallers = ReadBlockInfo(BlockInfoFilename);
    auto blockLabels = ReadBlockLabels(BlockInfoFilename);
    auto threadStarts = ReadThreadStarts(BlockInfoFilename);
    // load bitcode
    LLVMContext context;
    SMDiagnostic smerror;
    auto SourceBitcode = parseIRFile(BitcodeFileName, smerror, context);
    Format(SourceBitcode.get());
    // construct its callgraph
    map<int64_t, BasicBlock *> IDToBlock;
    map<int64_t, Value *> IDToValue;
    InitializeIDMaps(SourceBitcode.get(), IDToBlock, IDToValue);

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

    // generate sets of basic blocks for each kernel
    map<string, set<BasicBlock *>> kernelSets;
    for (auto kid : tasks)
    {
        for (auto bid : kid.second)
        {
            kernelSets[kid.first].insert(IDToBlock.at(bid));
        }
    }

    // maps a special string "KF", "IV", "BP" to a set of value IDs that will be specially colored for rendering
    // ["kernel function", "induction variable", "base pointer"]
    map<string, set<int64_t>> specialInstructions;
    // for each kernel, go through its blocks and find loads in them
    // we are only concerned with the "last" load instructions. These loads are the final loads in the serial DFG chain ie GEPs are not paid attention to
    try
    {
        specialInstructions["KF"] = findFunction(kernelSets);
        specialInstructions["IV"] = findState(kernelSets);
        specialInstructions["BP"] = findMemory(kernelSets);
    }
    catch (AtlasException& e)
    {
        spdlog::critical(e.what());
        return EXIT_FAILURE;
    }
    for (const auto &entry : specialInstructions.at("IV"))
    {
        specialInstructions.at("KF").erase(entry);
    }

    /* this section constructs the data flow and shared_ptr<ControlBlock> */
    // BBsubgraphs of the program
    set<shared_ptr<ControlBlock>, p_GNCompare> programFlow;
    // data flow of the program
    DataGraph dGraph;

    if (BuildDFG(SourceBitcode.get(), dynamicCG, blockToNode, programFlow, dGraph, specialInstructions, IDToBlock))
    {
        throw AtlasException("Failed to build DFG!");
    }
    auto taskCycles = getTasks(instanceJson, kernelJson, IDToBlock);

    // find the IVs in the program
    unsigned taskID = 0;
	for( const auto& t : taskCycles )
	{
        spdlog::info("Task "+to_string(taskID++));
		try
		{
    		auto vars = getInductionVariables(t);
            spdlog::info("Vars:");
            for( const auto& var : vars )
            {
                spdlog::info(var->dump()+" -> "+PrintVal(var->getNode()->getInst(), false));
            }
		}
		catch( AtlasException& e )
		{
			spdlog::critical(e.what());
		}
	}

    /*
    // print tasks with specialInstructions highlighted
    for (auto kern : tasks)
    {
        set<shared_ptr<ControlBlock>, p_GNCompare> kernelgraph;
        for (auto block : kern.second)
        {
            auto n = blockToNode.find(block);
            if( n == blockToNode.end() )
            { 
                spdlog::warn("Could not find a kernel block in the dynamic program structure!");
                continue;
            }
            auto flowBlock = programFlow.find(blockToNode.at(block)->NID);
            if( flowBlock == programFlow.end() )
            {
                spdlog::warn("Could not find a kernel block in the dynamic program dataflow!");
                continue;
            }
            kernelgraph.insert(*flowBlock);
        }
        ofstream DFGDot("DFG_kernel" + kern.first + ".dot");
        auto dataGraph = GenerateBBSubgraphDot(kernelgraph);
        DFGDot << dataGraph << "\n";
        DFGDot.close();
    }

    // output json file with special instruction information
    auto outputMap = specialInstructions;
    uint64_t staticInstructions  = 0;
    uint64_t dynamicInstructions = 0;
    uint64_t kernelInstructions  = 0;
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
            auto blockID = GetBlockID(cast<BasicBlock>(b));
            if( blockToNode.find(blockID) != blockToNode.end() )
            {
                dynamicInstructions += blockInstCount;
                bool found = false;
                for( const auto& kern : tasks )
                {
                    if( std::find(kern.second.begin(), kern.second.end(), blockID) != kern.second.end() )
                    {
                        kernelInstructions += blockInstCount;
                    }
                }
            }
        }
    }
    uint64_t labeledInstructions = 0;
    for( const auto& cat : specialInstructions )
    {
        for( const auto& entry : cat.second )
        {
            labeledInstructions++;
        }
    }
    nlohmann::json output = outputMap;
    output["Statistics"]["StaticInstCount"]  = staticInstructions;
    output["Statistics"]["DynamicInstCount"] = dynamicInstructions;
    output["Statistics"]["KernelInstCount"]  = kernelInstructions;
    output["Statistics"]["LabeledInstCount"] = labeledInstructions;
    
    // kernel function histogram
    map<string, uint64_t> hist;
    for( unsigned i = 0; i < static_cast<int>(Cyclebite::Graph::Operation::nop); i++ )
    {
        auto op = static_cast<Cyclebite::Graph::Operation>(i);
        hist[Cyclebite::Graph::OperationToString.at(static_cast<Cyclebite::Graph::Operation>(i))] = 0;
    }
    for( const auto& entry : specialInstructions["KF"] )
    {
        auto val = IDToValue.at(entry);
        if( auto inst = llvm::dyn_cast<Instruction>(val) )
        {
            try
            {
                hist.at(Cyclebite::Graph::OperationToString.at(GetOp(inst->getOpcode())))++;
            }
            catch( exception& e )
            {
                spdlog::critical("OpToString map failed on the following instruction: ");
                PrintVal(inst);
            }
        }
        else
        {
            hist.at(Cyclebite::Graph::OperationToString.at(Cyclebite::Graph::Operation::nop))++;
        }
    }
    output["Statistics"]["FunctionHistogram"] = hist;

    ofstream oStream(OutputFile);
    oStream << setw(4) << output;
    oStream.close();
    */

    return 0;
}
