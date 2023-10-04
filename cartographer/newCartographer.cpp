// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
#include "Util/IO.h"
#include "CallGraph.h"
#include "ControlGraph.h"
#include "Dijkstra.h"
#include "Graph.h"
#include "Hotcode.h"
#include "IO.h"
#include "Transforms.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <llvm/Support/CommandLine.h>
#include <queue>

#ifdef WINDOWS
#include <direct.h>
#define GetCurrentDir _getcwd
#else
#include <unistd.h>
#define GetCurrentDir getcwd
#endif

using namespace llvm;
using namespace std;
using namespace Cyclebite::Cartographer;
using namespace Cyclebite::Graph;
using json = nlohmann::json;

cl::opt<string> ProfileFileName("i", cl::desc("Specify bin file"), cl::value_desc(".bin filename"), cl::Required);
cl::opt<string> BitcodeFileName("b", cl::desc("Specify bitcode file"), cl::value_desc(".bc filename"), cl::Required);
cl::opt<string> BlockInfoFilename("bi", cl::desc("Specify BlockInfo.json file"), cl::value_desc(".json filename"), cl::Required);
cl::opt<string> LoopFileName("l", cl::desc("Specify Loopinfo.json file"), cl::value_desc(".json filename"), cl::init("Loopinfo.json"));
cl::opt<bool> HotCodeDetection("h", cl::desc("Perform hotcode detection"), cl::value_desc("Enable hot code detection only. Input profile must have markov order 1"), cl::init(false));
cl::opt<float> HotCodeThreshold("ht", cl::desc("Set hotcode threshold"), cl::value_desc("Set the threshold in which the hotcode algorithm will terminate. Should be a number between 0 and 1 (representing \% of runtime accounted for)"), cl::init(0.95f));
cl::opt<string> DotFile("d", cl::desc("Specify dot filename"), cl::value_desc("dot file"));
cl::opt<string> KernelPredictorScript("p", cl::desc("Specify path to label predictor script (should include the script name in the path)"), cl::value_desc("python file"));
cl::opt<string> OutputFilename("o", cl::desc("Specify output json"), cl::value_desc("kernel filename"), cl::Required);

extern uint32_t markovOrder;

int main(int argc, char **argv)
{
    // we measure the time taken for both the transforms section and the kernel virtualization section
    struct timespec start, end;
    while (clock_gettime(CLOCK_MONOTONIC, &start))
    {
    }
    cl::ParseCommandLineOptions(argc, argv);

    // static and dynamic information about the program structure
    auto blockCallers = ReadBlockInfo(BlockInfoFilename);
    auto blockLabels = ReadBlockLabels(BlockInfoFilename);
    auto threadStarts = ReadThreadStarts(BlockInfoFilename);
    auto SourceBitcode = ReadBitcode(BitcodeFileName);
    if (SourceBitcode == nullptr)
    {
        return EXIT_FAILURE;
    }
    // Construct static callgraph
    llvm::CallGraph staticCG(*SourceBitcode);
#ifdef DEBUG
    ofstream callGraphDot("StaticCallGraph.dot");
    auto staticCallGraph = GenerateCallGraph(staticCG);
    callGraphDot << staticCallGraph << "\n";
    callGraphDot.close();
#endif
    // map IDs to blocks and values
    map<int64_t, BasicBlock *> IDToBlock;
    map<int64_t, Value *> IDToValue;
    Cyclebite::Util::InitializeIDMaps(SourceBitcode.get(), IDToBlock, IDToValue);
    // construct program control graph and call graph
    ControlGraph cg;
    Cyclebite::Graph::CallGraph dynamicCG;
    getDynamicInformation(cg, dynamicCG, ProfileFileName, SourceBitcode, staticCG, blockCallers, threadStarts, IDToBlock, HotCodeDetection );
#ifdef DEBUG
    FindAllRecursiveFunctions(staticCG, cg, IDToBlock);
    FindAllRecursiveFunctions(dynamicCG, cg, IDToBlock);
#endif

    /// run hotcode structuring algorithms, if asked to do so
    if (HotCodeDetection)
    {
        auto hotCodeKernels = DetectHotCode(cg.getControlNodes(), HotCodeThreshold);
        EntropyInfo entropies;
        WriteKernelFile(cg, hotCodeKernels, IDToBlock, blockCallers, entropies, OutputFilename + "_HotCode.json", true);
        auto hotLoopKernels = DetectHotLoops(hotCodeKernels, cg, IDToBlock, LoopFileName);
        WriteKernelFile(cg, hotLoopKernels, IDToBlock, blockCallers, entropies, OutputFilename + "_HotLoop.json", true);
    }

    /// Transform dynamic control flow graph before structuring its tasks
    EntropyInfo entropies;
    entropies.start_entropy_rate = EntropyCalculation(cg.getControlNodes());
    entropies.start_total_entropy = TotalEntropy(cg.getControlNodes());
    entropies.start_node_count = (uint32_t)cg.node_count();
    entropies.start_edge_count = (uint32_t)cg.edge_count();
    ApplyCFGTransforms(cg, dynamicCG, false);
    entropies.end_entropy_rate = EntropyCalculation(cg.getControlNodes());
    entropies.end_total_entropy = TotalEntropy(cg.getControlNodes());
    entropies.end_node_count = (uint32_t)cg.node_count();
    entropies.end_edge_count = (uint32_t)cg.edge_count();
#ifdef DEBUG
    spdlog::info("STARTNODES: " + to_string(entropies.start_node_count));
    spdlog::info("TRANSFORMEDNODES: " + to_string(entropies.end_node_count));
    spdlog::info("STARTEDGES: " + to_string(entropies.start_edge_count));
    spdlog::info("TRANSFORMEDEDGES: " + to_string(entropies.end_edge_count));
    spdlog::info("STARTENTROPY: " + to_string(entropies.start_entropy_rate));
    spdlog::info("ENDENTROPY: " + to_string(entropies.end_entropy_rate));
    spdlog::info("STARTTOTALENTROPY: " + to_string(entropies.start_total_entropy));
    spdlog::info("ENDTOTALENTROPY: " + to_string(entropies.end_total_entropy));
#endif
    while (clock_gettime(CLOCK_MONOTONIC, &end))
    {
    }
    double totalTime = CalculateTime(&start, &end);
    spdlog::info("CARTOGRAPHERTRANSFORMTIME: " + to_string(totalTime));

    /// structure dynamic control flow graph
    while (clock_gettime(CLOCK_MONOTONIC, &start))
    {
    }
    auto kernels = FindMLCycles(cg, dynamicCG, true);
    while (clock_gettime(CLOCK_MONOTONIC, &end))
    {
    }
    totalTime = CalculateTime(&start, &end);
    spdlog::info("CARTOGRAPHERKERNELS: " + to_string(kernels.size()));
    spdlog::info("CARTOGRAPHERSEGMENTATIONTIME: " + to_string(totalTime) + "s");

    /// kernel processing and printing
    // labels for kernels
    for (const auto &kernel : kernels)
    {
        map<string, int64_t> labelVotes;
        labelVotes[""] = 0;
        for (const auto &node : kernel->getSubgraph())
        {
            for (const auto &block : node->blocks)
            {
                auto infoEntry = blockLabels.find(block);
                if (infoEntry != blockLabels.end())
                {
                    for (const auto &label : (*infoEntry).second)
                    {
                        if (labelVotes.find(label.first) == labelVotes.end())
                        {
                            labelVotes[label.first] = label.second;
                        }
                        else
                        {
                            labelVotes[label.first] += label.second;
                        }
                    }
                }
                else
                {
                    // no entry for this block, so votes for no label
                    labelVotes[""]++;
                }
            }
        }
        string maxVoteLabel;
        int64_t maxVoteCount = 0;
        for (const auto &label : labelVotes)
        {
            if (label.second > maxVoteCount)
            {
                maxVoteLabel = label.first;
                maxVoteCount = label.second;
            }
        }
        kernel->Label = maxVoteLabel;
    }
    WriteKernelFile(cg, kernels, IDToBlock, blockCallers, entropies, OutputFilename);
    if (!DotFile.empty())
    {
        auto unrolledGraph = reverseTransform_MLCycle(cg);
        ofstream dStream(DotFile);
        auto graphdot = GenerateDot(unrolledGraph);
        dStream << graphdot << "\n";
        dStream.close();
        /*ofstream tStream("SegmentedTransformedGraph.dot");
        graphdot = GenerateTransformedSegmentedDot(transformedGraph, kernels, (int)markovOrder);
        tStream << graphdot << "\n";
        tStream.close();*/
    }

    return 0;
}