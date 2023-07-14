#include "Hotcode.h"
#include "IO.h"
#include "Transforms.h"
#include "UnconditionalEdge.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using namespace llvm;
using namespace std;
using namespace TraceAtlas::Cartographer;
using namespace TraceAtlas::Graph;
using json = nlohmann::json;

constexpr uint64_t THRESHOLD_MAX_COLD = 256;
constexpr uint64_t THRESHOLD_MIN_HOT = 16;

set<std::shared_ptr<MLCycle>, KCompare> TraceAtlas::Cartographer::DetectHotCode(const set<std::shared_ptr<ControlNode>, p_GNCompare> &nodes, float hotThreshold)
{
    set<std::shared_ptr<MLCycle>, KCompare> kernels;
    // we are just detecting hot code
    // which will require the frequencies of each block
    map<int64_t, uint64_t> blockFrequencies;
    // accumulate block frequencies
    for (const auto &block : nodes)
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
    // Algorithm:
    // 1. Sort all blocks in descending order until 95% of the total block frequency is accounted for
    // 2. Partition the hot blocks based on who their successors are. These form "hot regions"
    auto BFpairs = std::vector<std::pair<int64_t, uint64_t>>();
    for (const auto &entry : blockFrequencies)
    {
        BFpairs.push_back(pair<int64_t, uint64_t>(entry.first, entry.second));
    }
    std::sort(BFpairs.begin(), BFpairs.end(), [](const pair<int64_t, uint64_t> &lhs, const pair<int64_t, uint64_t> &rhs) { return lhs.second > rhs.second; });
    uint64_t totalFrequency = 0;
    for (const auto &entry : BFpairs)
    {
        totalFrequency += entry.second;
    }
    if (BFpairs.empty())
    {
        spdlog::critical("No blocks were found in the input profile!");
        return kernels;
    }
    // step one, identify all hot blocks
    float accountedFor = 0.0;
    set<int64_t> hotBlocks;
    auto hbcount = hotBlocks.size();
    for (const auto &entry : BFpairs)
    {
        if (entry.second > THRESHOLD_MIN_HOT)
        {
            hotBlocks.insert(entry.first);
            accountedFor += (float)((float)entry.second / (float)totalFrequency);
        }
        // John [3/16/22]: hmmm, what about the case where every block has identical frequency... if our frequency counts have a large mode, we may get different results each time, depending on how ties are broken in the sort
        // one solution: take the entire mode
        if (accountedFor >= hotThreshold)
        {
            break;
        }
        else if (hbcount == hotBlocks.size())
        {
            // we didn't find any new blocks, meaning there aren't any more eligible blocks left
            break;
        }
        hbcount = hotBlocks.size();
    }
    // make sure we got all blocks that are above the max threshold
    for (const auto &entry : BFpairs)
    {
        if ((hotBlocks.find(entry.first) == hotBlocks.end()) && (entry.second > THRESHOLD_MAX_COLD))
        {
            hotBlocks.insert(entry.first);
        }
    }
    // step 2: group the hot blocks into kernels
    // condition 1: hot blocks have an edge from one to another: they belong to a group
    // condition 2: hot blocks are 1 block away from each other: they are grouped together and so is the non-hot block
    // John [3/16/22]: this is a graph coloring problem, in this case each HB just needs a parent kernel ID, so no copy is necessary
    vector<int64_t> tmpHotBlocks(hotBlocks.begin(), hotBlocks.end());
    for (auto hbIt = tmpHotBlocks.begin(); hbIt != tmpHotBlocks.end(); hbIt++)
    {
        // check if this block still needs to be evaluated (if its not in the hotblocks set anymore it either was not hot or already belongs to a kernel)
        if (hotBlocks.find(*hbIt) == hotBlocks.end())
        {
            // we've already been removed, move onto the next
            continue;
        }
        // check if this block is a neighbor to an existing kernel
        bool found = false;
        for (const auto k : kernels)
        {
            for (const auto &block : k->getSubgraph())
            {
                for (const auto &bn : block->getSuccessors())
                {
                    if (bn->getSnk()->NID == (uint64_t)*hbIt)
                    {
                        found = true;
                        break;
                    }
                }
                if (found)
                {
                    break;
                }
            }
            if (found)
            {
                k->addNode(*nodes.find(*hbIt));
                hotBlocks.erase(*hbIt);
                break;
            }
        }
        if (found)
        {
            continue;
        }

        auto newKern = make_shared<MLCycle>();
        kernels.insert(newKern);
        auto block = *nodes.find(*hbIt);
        // first condition, evaluate all successors and rope them into the kernel if they are hot
        for (const auto &nei : block->getSuccessors())
        {
            if (hotBlocks.find((int64_t)nei->getSnk()->NID) != hotBlocks.end())
            {
                auto newNode = *nodes.find(nei->getSnk()->NID);
                newKern->addNode(newNode);
                hotBlocks.erase((int64_t)nei->getSnk()->NID);
            }
        }
    }

    // even with the above checks, we can still have kernels that are next to each other
    // go through all kernels and see if you can merge them together
    // John [3/16/22]: these algorithms are graph coloring, with breadth first searches... so you can just use those algorithmic fundamentals to make these algorithms better
    vector<std::shared_ptr<MLCycle>> tmpKernels(kernels.begin(), kernels.end());
    for (auto kernIt = tmpKernels.begin(); kernIt != tmpKernels.end(); kernIt++)
    {
        if (kernels.find((*kernIt)->KID) == kernels.end())
        {
            continue;
        }
        for (auto neighborKernIt = next(kernIt); neighborKernIt != tmpKernels.end(); neighborKernIt++)
        {
            if (kernels.find((*neighborKernIt)->KID) == kernels.end())
            {
                continue;
            }
            auto kern = *kernIt;
            auto nKern = *neighborKernIt;
            bool match = false;
            for (const auto &block : kern->getSubgraph())
            {
                for (const auto &nei : block->getSuccessors())
                {
                    if (nKern->find(nei->getWeightedSnk()))
                    {
                        match = true;
                        break;
                    }
                }
                for (const auto &p : block->getPredecessors())
                {
                    if (nKern->find(p->getWeightedSrc()))
                    {
                        match = true;
                        break;
                    }
                }
                if (match)
                {
                    break;
                }
            }
            if (match)
            {
                kern->addNodes(nKern->getSubgraph());
                kernels.erase(nKern);
                break;
            }
        }
    }
    return kernels;
}

set<std::shared_ptr<MLCycle>, KCompare> TraceAtlas::Cartographer::DetectHotLoops(const set<std::shared_ptr<MLCycle>, KCompare> &hotKernels, const Graph::Graph &graph, const std::map<int64_t, llvm::BasicBlock *> &IDToBlock, const string &loopfilename)
{
    set<std::shared_ptr<MLCycle>, KCompare> kernels;
    set<StaticLoop, StaticLoopCompare> staticLoops;
    // read in loop information
    ifstream loopfile;
    json j;
    try
    {
        loopfile.open(loopfilename);
        loopfile >> j;
        loopfile.close();
    }
    catch (std::exception &e)
    {
        spdlog::critical("Couldn't open loop file " + string(loopfilename) + ": " + string(e.what()));
        return kernels;
    }

    map<int64_t, std::shared_ptr<ControlNode>> blockToNode;
    for (int i = 0; i < (int)j["Loops"].size(); i++)
    {
        // for now the loop type constraints are relaxed
        /*if (j[(uint64_t)i]["Type"] != 0)
        {
            continue;
        }*/
        // each i is an index for each loop in the file, each loop index maps to a list of block IDs and a loop type
        // for a list of types please see the top of TraceInfrastructure/Passes/LoopInfoDump.cpp
        auto newLoop = StaticLoop();
        newLoop.id = i;
        // type 0 represents a normal loop (ie eligible for hotloop detection)
        // any other type indicates a loop that is not eligible
        for (auto block : j["Loops"][(uint64_t)i]["Blocks"].get<vector<int64_t>>())
        {
            newLoop.blocks.insert(block);
        }
        // now map each loop's blocks to a node in the graph
        // we do this by finding an equivalent edge in the loop that is represented in the graph
        // this is dependent on the markov order
        for (auto &block : newLoop.blocks)
        {
            auto node = static_pointer_cast<ControlNode>(BlockToNode(graph, IDToBlock.at(block), NIDMap));
            if (node)
            {
                newLoop.nodes.insert(node);
            }
            // else it is likely dead code
        }
        staticLoops.insert(newLoop);
    }

    // detect hot loops
    // we always assume there is a 1:1 mapping between hot blocks and hot nodes
    // hotloop detection is a feature on top of hotcode, therefore the hotloop result is at least the hotcode result
    auto loops_copy = staticLoops;
    while (!staticLoops.empty())
    {
        auto loop = staticLoops.begin();
        bool matched = false;
        set<int64_t> intersect;
        for (const auto &currentKernel : hotKernels)
        {
            auto kernelBlocks = currentKernel->blocks;
            set_intersection(loop->blocks.begin(), loop->blocks.end(), kernelBlocks.begin(), kernelBlocks.end(), std::inserter(intersect, intersect.begin()));
            if (!intersect.empty())
            {
                // this loop intersects with hot blocks
                // therefore, create a hot loop kernel
                auto newKernel = make_shared<MLCycle>();
                newKernel->addNodes(currentKernel->getSubgraph());
                auto loopNodes = loop->nodes;
                newKernel->addNodes(loopNodes);
                // this line is going to add all dead blocks to the static loop finder
                newKernel->addBlocks(loop->blocks);
                kernels.insert(newKernel);
                staticLoops.erase(loop);
                matched = true;
                break;
            }
        }
        if (!matched)
        {
            staticLoops.erase(loop);
        }
    }
    // finally, for any hot kernel that did not find a loop, add them to the result
    // as of 2/3/2022 we don't do this anymore. It gives the hotloop method more credence than it deserves
    // even though this is what prior work did, we are trying to make comparisions to methods that are common to the research community. Therefore we don't want to optimize the hotloop result: we want to demonstrate its limitations
    /*for( const auto& hk : hotKernels )
    {
        // try to find a loop that represents this hotkernel
        bool matched = false;
        for( const auto& sl : loops_copy)
        {
            // in order for a loop to match a hotkernel, the loop blocks have to cover all blocks from the hotkernel
            // the first match we see is enough to end the algorithm because we're just looking to see if the hotcode has been included in the result (hotloops are a superset of hotcode result)
            set<int64_t> intersect;
            auto hkblocks = hk->getBlocks();
            set_intersection(sl.blocks.begin(), sl.blocks.end(), hkblocks.begin(), hkblocks.end(), std::inserter(intersect, intersect.begin()));
            if( intersect.size() >= hk->getBlocks().size() )
            {
                matched = true;
                break;
            }
        }
        if( !matched )
        {
            // include the hot kernel
            auto newKernel = new MLCycle(newKernelID++);
            newKernel->addNodes(hk->subgraph);
            newKernel->addNodes(hk->subgraph);
            kernels.insert(newKernel);
        }
    }*/
    return kernels;
}