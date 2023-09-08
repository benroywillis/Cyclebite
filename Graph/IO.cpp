#include "IO.h"
#include "Util/Annotate.h"
#include "Util/Print.h"
#include "CallEdge.h"
#include "CallGraph.h"
#include "CallNode.h"
#include "ControlBlock.h"
#include "ControlGraph.h"
#include "DataGraph.h"
#include "Dijkstra.h"
#include "MLCycle.h"
#include "ReturnEdge.h"
#include "Transforms.h"
#include "ImaginaryNode.h"
#include "ImaginaryEdge.h"
#include "VirtualEdge.h"
#include "VirtualNode.h"
#include "llvm/IR/CFG.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <llvm/IR/Statepoint.h>
#include <nlohmann/json.hpp>

using namespace std;
using namespace llvm;
using namespace Cyclebite::Graph;

/// Cutoff threshold for number of edges in an unabridged highlighted graph
constexpr uint64_t MAX_EDGE_UNABRIDGED = 2000;

uint32_t markovOrder;
/// Maps a vector of basic block IDs to a node ID
map<vector<uint32_t>, uint64_t> Cyclebite::Graph::NIDMap;
/// Maps each unique instruction to its datanode
map<const llvm::Value *, const shared_ptr<DataValue>> Cyclebite::Graph::DNIDMap;
/// Maps each basic block to its ControlBlock
std::map<const llvm::BasicBlock*, const std::shared_ptr<ControlBlock>> Cyclebite::Graph::BBCBMap;

/// Sets the number of decimal places in a float-to-string conversion
std::string to_string_float(float f, int precision = 3)
{
    std::stringstream stream;
    stream << std::fixed << std::setprecision(precision) << f;
    return stream.str();
}

/// @brief Reads an input profile
///
/// @param graph    Structure that will hold the raw profile input. Raw profile input only has control edges and Conditional/Unconditional nodes. This profile may not pass all checks in Cyclebite::Graph::Checks because of function pointers.
/// @param filename Profile filename
/// @param HotCodeDetection     Flag enabling hotcode detection checks on the profile. Right now there is only one check: the input profile must have markov order 1
int Cyclebite::Graph::BuildCFG(Graph &graph, const std::string &filename, bool HotCodeDetection)
{
    // first initialize the graph to all the blocks in the
    FILE *f = fopen(filename.data(), "rb");
    if (!f)
    {
        return 1;
    }
    // first word is a uint32_t of the markov order of the graph
    fread(&markovOrder, sizeof(uint32_t), 1, f);

    // second word is a uint32_t of the total number of blocks in the graph (each block may or may not be connected to the rest of the graph)
    uint32_t blocks;
    fread(&blocks, sizeof(uint32_t), 1, f);

    // third word is a uint32_t of how many edges there are in the file
    uint32_t numEdges;
    fread(&numEdges, sizeof(uint32_t), 1, f);

    if (HotCodeDetection)
    {
        if (markovOrder != 1)
        {
            spdlog::critical("Hot code detection can only be performed on an input profile that has markov order 1!");
            return 1;
        }
    }

    // list of source nodes for a new edge being read from the input profile
    // nodes go in order of least recent to most recent
    uint32_t newSources[markovOrder];
    for (uint32_t i = 0; i < markovOrder; i++)
    {
        newSources[i] = 0;
    }
    // place to read in a given sink ID from the input profile
    uint32_t sink = 0;
    // place to read in a given edge frequency from the input profile
    uint64_t frequency = 0;
    for (uint32_t i = 0; i < numEdges; i++)
    {
        // source node IDs go in order or least recent to most recent
        fread(&newSources, sizeof(uint32_t), markovOrder, f);
        fread(&sink, sizeof(uint32_t), 1, f);
        fread(&frequency, sizeof(uint64_t), 1, f);
        std::vector<uint32_t> newSourceIDs;
        std::shared_ptr<ControlNode> sourceNode = nullptr;
        for (auto &id : newSources)
        {
            newSourceIDs.push_back(id);
        }
        if (NIDMap.find(newSourceIDs) == NIDMap.end())
        {
            sourceNode = make_shared<ControlNode>();
            NIDMap[newSourceIDs] = sourceNode->NID;
            sourceNode->blocks.insert(newSourceIDs.begin(), newSourceIDs.end());
            sourceNode->originalBlocks = newSourceIDs;
            graph.addNode(sourceNode);
        }
        else
        {
            sourceNode = static_pointer_cast<ControlNode>(graph.getOriginalNode(NIDMap[newSourceIDs]));
        }
        if (sourceNode == nullptr)
        {
            throw AtlasException("Found a node described in an edge that does not exist in the BBID space!");
        }

        // now synthesize the sink neighbor of this node, if a node for it doesn't yet exist
        // first we insert all newSourceIDs except the oldest one
        // then we insert the sink node ID to complete all IDs for the neighbor node
        vector<uint32_t> neighborSourceIDs(next(newSourceIDs.begin()), newSourceIDs.end());
        neighborSourceIDs.push_back(sink);
        std::shared_ptr<ControlNode> sinkNode = nullptr;
        if (NIDMap.find(neighborSourceIDs) == NIDMap.end())
        {
            sinkNode = make_shared<ControlNode>();
            NIDMap[neighborSourceIDs] = sinkNode->NID;
            sinkNode->blocks.insert(neighborSourceIDs.begin(), neighborSourceIDs.end());
            sinkNode->originalBlocks = neighborSourceIDs;
            graph.addNode(sinkNode);
        }
        else
        {
            sinkNode = static_pointer_cast<ControlNode>(graph.getOriginalNode(NIDMap[neighborSourceIDs]));
        }
        if (sinkNode == nullptr)
        {
            throw AtlasException("Could not find a node in the graph that matches the NID found to map to this neighbor!");
        }
        if (sourceNode->isPredecessor(sinkNode))
        {
            throw AtlasException("This sink node ID is already a neighbor of this source node!");
        }
        // each edge is a basic edge with a frequency count and two nodes, upgrading to more specific edge types like ConditionalEdge and CallEdge are done in UpgradeEdges()
        auto newEdge = make_shared<UnconditionalEdge>(frequency, sourceNode, sinkNode);
        graph.addEdge(newEdge);
        sourceNode->addSuccessor(newEdge);
        sinkNode->addPredecessor(newEdge);
    }
    fclose(f);
    return 0;
}

/// @brief Finds the destination nodes of null function calls and puts then in snkNodes
///
/// @param srcNode      The ControlNode that represents the basic block that contains the call instruction
/// @param snkNodes     The destination nodes of the function call. This can be more than one node because null function calls can take on multiple values during runtime.
/// @param call         Call instruction in question
/// @param graph        The control graph that contains the input profile
/// @param blockCallers Maps a calling block ID to a vector of its observed destination blocks
/// @param IDToBlock    Maps a block ID to a basic block pointer
void resolveNullFunctionCall(const shared_ptr<ControlNode> &srcNode, set<shared_ptr<ControlNode>, p_GNCompare> &snkNodes, const CallBase *call, const Graph &graph, const std::map<int64_t, std::vector<int64_t>> &blockCallers, const std::map<int64_t, llvm::BasicBlock *> &IDToBlock)
{
    // blockCallers should tell us which basic block this null function call goes to next

    // there is a corner case here where libc functions can appear to be null when in fact they are statically determinable
    // this can happen if somebody uses a libc function but doesn't include the corresponding header (this will show up as a warning about an undeclared function)
    // the linker will make this all okay, but within the bitcode module for some reason the LLVM api will return null, even when a function pointer is not used
    // example: Algorithms/UnitTests/SimpleRecursion (fibonacci), the atoi() function will appear to be a null function call unless #include <stdlib.h> is at the top
    // the below checks will break because the function itself will appear "empty" in the llvm IR (it is from libc and we don't profile that)
    // since the function call is not profiled, we will not get an entry in blockCallers for it
    // to help prevent this, the user can pass -Werror
    // at this point, the only way I can think of to detect this case is to see if there is actually a function name (with a preceding @ symbol)
    // the above mechanism will fail if the null function call has a global variable in its arguments list (globals are preceded by @ too)
    auto instString = PrintVal(call, false);
    if (blockCallers.find(GetBlockID(call->getParent())) != blockCallers.end())
    {
        // this is a multi-dimensional problem, even with basic block splitting
        // a function pointer is allowed to call any function that matches a signature
        // when a function pointer goes to more than one function, we have to be able to enumerate that case here
        for (auto callee : blockCallers.at(GetBlockID(call->getParent())))
        {
            auto n = BlockToNode(graph, IDToBlock.at(callee), NIDMap);
            if( n )
            {
                snkNodes.insert(static_pointer_cast<ControlNode>(n));
            }
        }
        // since we have an entry for this null function call, we already know the function call is non-empty
        // see TraceInfrastructure/Backend/BackendMarkov.cpp:MarkovIncrement for more information on this guarantee
    }
    else if (instString.find('@') != std::string::npos)
    {
        // this is likely the corner case explained above, so we skip
        // John: 4/20/22, we should keep track of this, throw a warning (to measure the nature of this phenomenon [is it just libc, how prevalent is it])
        spdlog::warn("Found a statically determinable function call that appeared to be null. This is likely caused by a lack of declaration in the original source file.");
    }
    else
    {
        // this case could be due to either an empty function being called (a function that isn't in the input bitcode module) or profiler error... There really isn't any way of us knowing at this stage
#ifdef DEBUG
        PrintVal(call->getParent());
        spdlog::warn("Blockcallers did not contain information for a null function call observed in the dynamic profile. This could be due to an empty function or profiler error.");
        set<BasicBlock *> BBSuccs;
        for (unsigned i = 0; i < call->getParent()->getTerminator()->getNumSuccessors(); i++)
        {
            BBSuccs.insert(call->getParent()->getTerminator()->getSuccessor(i));
        }
        for (const auto &succ : srcNode->getSuccessors())
        {
            auto block = NodeToBlock(succ->getWeightedSnk(), IDToBlock);
            if (BBSuccs.find(block) == BBSuccs.end())
            {
                spdlog::critical("Profiler missed a null function call");
                snkNodes.insert(succ->getWeightedSnk());
            }
        }
#endif
    }
}

void buildFunctionSubgraph(shared_ptr<CallEdge> &newCall, const Graph &graph, const std::map<int64_t, std::vector<int64_t>> &blockCallers, const std::map<int64_t, llvm::BasicBlock *> &IDToBlock, const BasicBlock *functionBlock)
{
    // this section builds out the function subgraph in dynamic nodes
    // the subgraph should include all functions below this one
    deque<const llvm::Function *> Q;
    set<const llvm::Function *> covered;
    Q.push_front(functionBlock->getParent());
    covered.insert(functionBlock->getParent());
    while (!Q.empty())
    {
        for (auto fb = Q.front()->begin(); fb != Q.front()->end(); fb++)
        {
            auto n = BlockToNode(graph, llvm::cast<llvm::BasicBlock>(fb), NIDMap);
            if( n )
            {
                newCall->rets.functionNodes.insert(static_pointer_cast<ControlNode>(n));
                // parse through instructions to find embedded function calls
                for (auto fi = fb->begin(); fi != fb->end(); fi++)
                {
                    if (auto call = llvm::dyn_cast<CallBase>(fi))
                    {
                        if (call->getCalledFunction())
                        {
                            if (!call->getCalledFunction()->empty())
                            {
                                if (covered.find(call->getCalledFunction()) == covered.end())
                                {
                                    Q.push_back(call->getCalledFunction());
                                    covered.insert(call->getCalledFunction());
                                }
                            }
                        }
                        else if (blockCallers.find(GetBlockID(llvm::cast<llvm::BasicBlock>(fb))) != blockCallers.end())
                        {
                            for (auto callee : blockCallers.at(GetBlockID(llvm::cast<llvm::BasicBlock>(fb))))
                            {
                                auto calleeParent = IDToBlock.at(callee)->getParent();
                                if (!calleeParent->empty())
                                {
                                    if (covered.find(calleeParent) == covered.end())
                                    {
                                        Q.push_back(calleeParent);
                                        covered.insert(calleeParent);
                                    }
                                    // else we have already touched this parent
                                }
                                // else this is an empty function and won't be in the input profile
                            }
                        }
                        // else we don't care
                    } // if instruction is a callbase
                    // else we don't care about it
                } // for each block in the function
            }     // if the function block maps to a node in the profile
            // else it is likely dead code
        } // for each block in the front of the Q
        Q.pop_front();
    } // recursive algorithm
}

/// it is also possible for a function to exit through something that is not a return inst (like a call to libc exit())
/// this loop looks for edges that leave the function subgraph and determines if they should also by added to the rets structure
void findUnconventionalExits(shared_ptr<CallEdge> &newCall)
{
    for (const auto &node : newCall->rets.functionNodes)
    {
        for (const auto &succ : node->getSuccessors())
        {
            if (auto lastVE = dynamic_pointer_cast<VirtualNode>(succ->getSnk()))
            {
                // this is the virtual edge that represents the return from the program, add it to the dynamicRets if necessary
                if (newCall->rets.dynamicRets.find(succ) == newCall->rets.dynamicRets.end())
                {
                    newCall->rets.staticExits.insert(succ->getWeightedSrc());
                    newCall->rets.dynamicExits.insert(succ->getWeightedSrc());
                    newCall->rets.staticRets.insert(succ);
                    newCall->rets.dynamicRets.insert(succ);
                }
            }
        }
    }
}

/// Dynamic return edges are nuanced in the fact that they don't return to the caller basic block, they return to a successor of the caller basic block
void TransformDynamicReturnEdges(shared_ptr<CallEdge> &newCall, Graph &graph)
{
    set<shared_ptr<ReturnEdge>, GECompare> retEdges;
    auto origRets = newCall->rets.dynamicRets;
    for (auto origRet : origRets)
    {
        auto newRet = make_shared<ReturnEdge>(origRet->getFreq(), origRet->getWeightedSrc(), origRet->getWeightedSnk(), newCall);
        auto src = origRet->getWeightedSrc();
        auto snk = origRet->getWeightedSnk();
        src->removeSuccessor(origRet);
        src->addSuccessor(newRet);
        snk->removePredecessor(origRet);
        snk->addPredecessor(newRet);
        graph.removeEdge(origRet);
        graph.addEdge(newRet);
        newCall->rets.dynamicRets.erase(origRet);
        newCall->rets.dynamicRets.insert(newRet);
        retEdges.insert(newRet);
    }
    for (auto ret : retEdges)
    {
        uint64_t sum = 0;
        for (const auto &succ : ret->getWeightedSrc()->getSuccessors())
        {
            sum += succ->getFreq();
        }
        ret->setWeight(sum);
    }
}

/// @brief Implements imaginary edges
///
/// Imaginary nodes and edges mark the beginning and end of main
/// They also  fill in the gaps that are created by multithreaded applications
/// For example when threads are allowed to terminate without a join (pthread_exit)
/// Cyclebyte fills in these kinds of gaps with imaginary edges, which point (for example) from the ends of a thread to the imaginary edge at the end of main
shared_ptr<ControlNode> AddImaginaryEdges(llvm::Module* sourceBitcode, Graph& graph, std::set<int64_t> threadStarts)
{
    // here we add the imaginary nodes and edges that precede and succeed the main function
    // this must happen before we put imaginary edges at the end of threads
    shared_ptr<ImaginaryNode> firstFirstNode = make_shared<ImaginaryNode>();
    shared_ptr<ImaginaryNode> lastLastNode = make_shared<ImaginaryNode>();
    graph.addNode(firstFirstNode);
    graph.addNode(lastLastNode);
    shared_ptr<ControlNode> terminator = nullptr;
    for( auto fi = sourceBitcode->begin(); fi != sourceBitcode->end(); fi++ )
    {
        if( fi->getName() == "main" )
        {
            auto firstNode = static_pointer_cast<ControlNode>(BlockToNode(graph, llvm::cast<BasicBlock>(fi->begin()), NIDMap));
            auto zeroEdge = make_shared<ImaginaryEdge>(firstFirstNode, firstNode);
            static_pointer_cast<GraphNode>(firstNode)->addPredecessor(zeroEdge);
            firstFirstNode->addSuccessor(zeroEdge);
            graph.addNode(firstFirstNode);
            graph.addEdge(zeroEdge);

            // while main is guaranteed to start on its first node, it is not guaranteed to end on its last
            // in fact, the program isn't event guaranteed to exit within main (lib exit() function, for example)
            // thus, to find the true exit of the program, we have to carry out a series of evaluations
            // first, we investigate main to see if the program exit occurred in here
            for( auto bi = fi->begin(); bi != fi->end(); bi++ )
            {
                // we know that any block within main who has no successors is the exit of the program
                // this is because the dynamic profile guarantees that the node within main's context that has no successors must be the exit
                auto node = BlockToNode(graph, llvm::cast<BasicBlock>(bi), NIDMap);
                if( node )
                {
                    if( node->getSuccessors().empty() )
                    {
                        terminator = static_pointer_cast<ControlNode>(node);
                        break;
                    }
                }
            }
            // second, if the termination did not occur in main, we have to search for libc::exit()
            if( !terminator )
            {
                throw AtlasException("Cannot yet handle the case where the program terminates outside main!");
            }
            auto lastLastEdge = make_shared<ImaginaryEdge>(terminator, lastLastNode);
            lastLastNode->addPredecessor(lastLastEdge);
            static_pointer_cast<GraphNode>(terminator)->addSuccessor(lastLastEdge);
            graph.addEdge(lastLastEdge);
        }
    }
    // here we add imaginary edges from the ends of a thread launch to the imaginary node at the end of main
    for( auto fi = sourceBitcode->begin(); fi != sourceBitcode->end(); fi++ )
    {
        if( !fi->empty() )
        {
            // threads must start at functions, so the function entrance block should be in threadStarts if this function was the start of a new thread
            auto BB = llvm::cast<BasicBlock>(fi->begin());
            auto ID = GetBlockID(BB);
            if( threadStarts.find(ID) != threadStarts.end() )
            {
                // get the last block in the function
                // this can be found in the call instruction that precedes the first node
                set<shared_ptr<ControlNode>, p_GNCompare> returnNodes;
                for( const auto& pred : BlockToNode(graph, BB, NIDMap)->getPredecessors() )
                {
                    if( auto call = dynamic_pointer_cast<CallEdge>(pred) )
                    {
                        returnNodes.insert(call->rets.staticExits.begin(), call->rets.staticExits.end());
                    }
                }
                for( auto& ret : returnNodes )
                {
                    // add an imaginary edge between this node and the imaginary node at the end of main
                    auto imRet = make_shared<ImaginaryEdge>(ret, lastLastNode);
                    static_pointer_cast<GraphNode>(ret)->addSuccessor(imRet);
                    lastLastNode->addPredecessor(imRet);
                    graph.addEdge(imRet);
                }
            }
        }
    }
    return terminator;
}

/// @brief This function reads through all edges in the dynamic profile and upgrade UnconditionalEdges to conditional edges, call/return edges, etc
///
/// @param sourceBitcode    The formatted bitcode that was the source LLVM IR for the profile
/// @param graph            A raw profile that has been turned into a graph. By the end of this method, graph will pass all checks in Cyclebite::Graph::Checks
/// @param blockCallers     A map connecting caller basic blocks to their callees
/// @param IDToBlock        A map connecting basic block IDs to an llvm::BasicBlock pointer
void UpgradeEdges(const llvm::Module *sourceBitcode, Graph &graph, const std::map<int64_t, std::vector<int64_t>> &blockCallers, const std::map<int64_t, llvm::BasicBlock *> &IDToBlock)
{
    // for each function in the bitcode, parse its static structures (function calls, branch instructions) and inject that information into the dynamic graph
    // 0. Put imaginary nodes at start and end of main
    // 1. Upgrade conditional branch
    // 2. Upgrade function call edge
    // 3. Upgrade return edge
    for (auto fi = sourceBitcode->begin(); fi != sourceBitcode->end(); fi++)
    {
        for (auto bi = fi->begin(); bi != fi->end(); bi++)
        {
            auto BB = cast<BasicBlock>(bi);
            // this statement makes sure this basic block was observed in the profile
            auto BBnode = BlockToNode(graph, BB, NIDMap);
            if (!BBnode)
            {
                continue;
            }

            // Step one: conditional branch upgrade
            if (BB->getTerminator()->getNumSuccessors() > 1)
            {
                // find this edge in the input graph and upgrade it to a conditional edge
                auto srcNode = BBnode;
                vector<shared_ptr<GraphNode>> snkNodes;
                for (unsigned int i = 0; i < BB->getTerminator()->getNumSuccessors(); i++)
                {
                    auto succ = BB->getTerminator()->getSuccessor(i);
                    auto snkNode = BlockToNode(graph, succ, NIDMap);
                    if (snkNode)
                    {
                        snkNodes.push_back(snkNode);
                    }
                }
                if (snkNodes.size() > 1)
                {
                    uint64_t sum = 0;
                    set<shared_ptr<ConditionalEdge>, GECompare> newEdges;
                    for (auto snk : snkNodes)
                    {
                        auto finderEdge = make_shared<UnconditionalEdge>(0, srcNode, snk);
                        if (graph.find(finderEdge))
                        {
                            auto origEdge = graph.getOriginalEdge(finderEdge);
                            shared_ptr<ConditionalEdge> newEdge = nullptr;
                            if( auto ue = dynamic_pointer_cast<UnconditionalEdge>(origEdge) )
                            {
                                newEdge = make_shared<ConditionalEdge>(*ue);
                            }
                            else
                            {
                                continue;
                            }
                            auto it = newEdges.insert(newEdge);
                            if (!it.second)
                            {
                                // the static code mapped to the same destination more than once
                                // update the edge that already represents the current
                                auto newFreq = newEdge->getFreq() + it.first->get()->getFreq();
                                auto replaceEdge = make_shared<ConditionalEdge>(newFreq, newEdge->getWeightedSrc(), newEdge->getWeightedSnk());
                                newEdges.erase(newEdge);
                                newEdges.insert(replaceEdge);
                                newEdge = replaceEdge;
                            }
                            srcNode->removeSuccessor(origEdge);
                            snk->removePredecessor(origEdge);
                            graph.removeEdge(origEdge);
                            srcNode->addSuccessor(newEdge);
                            snk->addPredecessor(newEdge);
                            graph.addEdge(newEdge);
                            if( auto ue = dynamic_pointer_cast<UnconditionalEdge>(origEdge) )
                            {
                                sum += ue->getFreq();
                            }
                        } // else this edge was not observed in the dynamic profile, thus we skip it
                    }
                    for (auto newEdge : newEdges)
                    {
                        newEdge->setWeight(sum);
                    }
                } // else the number of sink nodes observed in the profile was not sufficient for a conditional edge
            }
            // check to see if the dynamic graph implies a condition, perhaps not present in the static code, that determines the next state
            // for example, when an empty function conditionally calls a non-empty function within its execution
            // example: OpenCV/qrcode inflate() is dead and conditionally called png_zalloc()
            // another example: a function call that accepts a pointer and takes on multiple values during execution
            else if (BBnode->getSuccessors().size() > 1)
            {
                // make the edges conditional, even though we don't know where the condition actually is
                auto srcNode = BBnode;
                uint64_t sum = 0;
                set<shared_ptr<ConditionalEdge>, GECompare> newEdges;
                auto succCopy = BBnode->getSuccessors();
                for (auto &succ : succCopy)
                {
                    auto snk = succ->getSnk();
                    auto finderEdge = make_shared<UnconditionalEdge>(0, srcNode, snk);
                    if (graph.find(finderEdge))
                    {
                        auto origEdge = graph.getOriginalEdge(finderEdge);
                        shared_ptr<ConditionalEdge> newEdge = nullptr;
                        if( auto ue = dynamic_pointer_cast<UnconditionalEdge>(origEdge) )
                        {
                            newEdge = make_shared<ConditionalEdge>(*ue);
                        }
                        else
                        {
                            continue;
                        }
                        auto it = newEdges.insert(newEdge);
                        if (!it.second)
                        {
                            // the static code mapped to the same destination more than once
                            // update the edge that already represents the current
                            auto newFreq = newEdge->getFreq() + it.first->get()->getFreq();
                            auto replaceEdge = make_shared<ConditionalEdge>(newFreq, newEdge->getWeightedSrc(), newEdge->getWeightedSnk());
                            newEdges.erase(newEdge);
                            newEdges.insert(replaceEdge);
                            newEdge = replaceEdge;
                        }
                        srcNode->removeSuccessor(origEdge);
                        snk->removePredecessor(origEdge);
                        graph.removeEdge(origEdge);
                        srcNode->addSuccessor(newEdge);
                        snk->addPredecessor(newEdge);
                        graph.addEdge(newEdge);
                        if( auto ue = dynamic_pointer_cast<UnconditionalEdge>(origEdge) )
                        {
                            sum += ue->getFreq();
                        }
                    } // else this edge was not observed in the dynamic profile, thus we skip it
                }
                for (auto newEdge : newEdges)
                {
                    newEdge->setWeight(sum);
                }
            }
            // else this is just an unconditional edge
        }
    }

    // Step 2: Call instructions
    set<const CallBase *> calls;
    set<const Instruction *> operandCalls;
    for (auto fi = sourceBitcode->begin(); fi != sourceBitcode->end(); fi++)
    {
        for (auto bi = fi->begin(); bi != fi->end(); bi++)
        {
            for (auto ii = bi->begin(); ii != bi->end(); ii++)
            {
                if (auto call = dyn_cast<CallBase>(ii))
                {
                    if (auto cb = dyn_cast<CallBrInst>(ii))
                    {
                        throw AtlasException("Cannot handle goto call instructions!");
                    }
                    /*else if( auto gc = dyn_cast<GCStatepointInst>(ii) )
                    {
                        // this doesn't seem to be supported in LLVM9
                        throw AtlasException("Cannot handle GCStatepoint instructions!");
                    }*/
                    calls.insert(call);
                }
                // function calls can hide in the operands of instructions
                // for example "trampoline" instructions and operators for casting
                // to do this effectively, we have to use a recursive method to go all the way down the operand chain
                deque<const User *> Q;
                set<const User *> covered;
                Q.push_front(cast<Instruction>(ii));
                covered.insert(cast<Instruction>(ii));
                while (!Q.empty())
                {
                    for (unsigned i = 0; i < Q.front()->getNumOperands(); i++)
                    {
                        if (auto op = dyn_cast<User>(Q.front()->getOperand(i)))
                        {
                            if (covered.find(op) == covered.end())
                            {
                                Q.push_back(op);
                                covered.insert(op);
                            }
                            for (unsigned j = 0; j < op->getNumOperands(); j++)
                            {
                                if (auto ci = dyn_cast<CallBase>(op->getOperand(j)))
                                {
                                    if (covered.find(ci) == covered.end())
                                    {
                                        calls.insert(ci);
                                    }
                                }
                                else if (auto f = dyn_cast<Function>(op->getOperand(j)))
                                {
                                    // we are interested in the uses of the operand with this function call
                                    for (auto use : op->users())
                                    {
                                        if (auto useInst = dyn_cast<Instruction>(use))
                                        {
                                            operandCalls.insert(useInst);
                                        }
                                    }
                                }
                            }
                        }
                    }
                    Q.pop_front();
                }
            }
        }
    }

    // now upgrade the call instruction edges
    for (const auto &call : calls)
    {
        auto BB = call->getParent();
        auto BBnode = static_pointer_cast<ControlNode>(BlockToNode(graph, BB, NIDMap));
        shared_ptr<ControlNode> srcNode = nullptr;
        set<shared_ptr<ControlNode>, p_GNCompare> snkNodes;
        // we attempt to find an edge in the graph that represents this function call
        // we should have a direct mapping between this caller basic block and the entrance block of the function
        srcNode = static_pointer_cast<ControlNode>(BlockToNode(graph, BB, NIDMap));
        if (srcNode == nullptr)
        {
            continue;
        }
        // if we can statically determine the callee we can just pick it
        if (call->getCalledFunction())
        {
            if (!call->getCalledFunction()->empty())
            {
                if( blockCallers.find(GetBlockID(BB)) != blockCallers.end() )
                {
                    for (auto callee : blockCallers.at(GetBlockID(BB)))
                    {
                        snkNodes.insert(static_pointer_cast<ControlNode>(BlockToNode(graph, IDToBlock.at(callee), NIDMap)));
                    }
                }
            }
            else
            {
#ifdef DEBUG
                spdlog::info("The following instruction calls an empty function:");
                PrintVal(call);
#endif
                continue;
            }
        }
        else
        {
            resolveNullFunctionCall(srcNode, snkNodes, call, graph, blockCallers, IDToBlock);
        }
        for (auto snkNode : snkNodes)
        {
            // now we upgrade the edge
            auto finderEdge = make_shared<UnconditionalEdge>(0, srcNode, snkNode);
            if (graph.find(finderEdge))
            {
                if( dynamic_pointer_cast<UnconditionalEdge>(graph.getOriginalEdge(finderEdge)) == nullptr )
                {
                    continue;
                }
                auto origEdge = static_pointer_cast<UnconditionalEdge>(graph.getOriginalEdge(finderEdge));
                auto newCall = make_shared<CallEdge>(*origEdge);
                newCall->rets.callerNode = BBnode;
                auto functionBlock = NodeToBlock(snkNode, IDToBlock);
                newCall->rets.f = functionBlock->getParent();
                buildFunctionSubgraph(newCall, graph, blockCallers, IDToBlock, functionBlock);

                // the snk node of the return edge is just the BB with the function call inst
                // to find the src node of the return edge of this call edge, we have to look through all return instructions of the called function
                auto firstFunctionBlock = NodeToBlock(newCall->getWeightedSnk(), IDToBlock);
                set<llvm::Instruction *> exits;
                for (auto block = firstFunctionBlock->getParent()->begin(); block != firstFunctionBlock->getParent()->end(); block++)
                {
                    if (auto ret = llvm::dyn_cast<ReturnInst>(block->getTerminator()))
                    {
                        exits.insert(ret);
                    }
                    else if( auto inv = llvm::dyn_cast<InvokeInst>(block->getTerminator()) )
                    {

                    }
                    else if( auto callb = llvm::dyn_cast<CallBrInst>(block->getTerminator()) )
                    {
                        throw AtlasException("Cannot handle callbr instruction terminators!");
                    }
                    else if( auto res = llvm::dyn_cast<ResumeInst>(block->getTerminator()) )
                    {
                        // resume instructions return the control flow to the calling invoke inst
                        // ie a resume instruction is the instruction that tells the calling invoke inst to go to the unwind destination
                        // whereas if a ret instruction gets control back to the invoke, the normal destination is taken
                        exits.insert(res);
                    }
                    else if( auto catchsw = llvm::dyn_cast<CatchSwitchInst>(block->getTerminator()) )
                    {
                        
                    }
                    else if( auto catchret = llvm::dyn_cast<CatchReturnInst>(block->getTerminator()) )
                    {
                        
                    }
                    else if( auto cleanret = llvm::dyn_cast<CleanupReturnInst>(block->getTerminator()) )
                    {
                        
                    }
                    else if( auto ur = llvm::dyn_cast<UnreachableInst>(block->getTerminator()) )
                    {
                        
                    }
                    else
                    {
                        // it is a terminator we do not care about
                    }
                }
                // for each basicblock with a ret instruction, find its dynamic node, and build out the dynamic equivalents of the return edge
                // the dynamic equivalent of a return edge is the block(s) that follows the function caller block
                // this phenomenon is caused by the fact that the profiler does not record the return edge (this would create a control flow cycle that starts and ends with the caller basic block)
                for (auto &exit : exits)
                {
                    auto snk = static_pointer_cast<ControlNode>(BlockToNode(graph, exit->getParent(), NIDMap));
                    if (snk)
                    {
                        // static information for the calledge, mapped to entities in the dynamic graph
                        newCall->rets.staticExits.insert(snk);
                        newCall->rets.staticRets.insert(make_shared<UnconditionalEdge>(newCall->getFreq(), snk, BBnode));
                        // the edge that exists in the dynamic graph points from the return edge src node to a node that occurs after the caller node
                        // in order to find this edge, we have to ask the static code which blocks can come after the caller block
                        // then map those successor blocks (of the caller block) to dynamic edges (from the callee return node to the caller node successor)
                        for (unsigned int i = 0; i < BB->getTerminator()->getNumSuccessors(); i++)
                        {
                            auto succNode = static_pointer_cast<ControlNode>(BlockToNode(graph, BB->getTerminator()->getSuccessor(i), NIDMap));
                            if (succNode)
                            {
                                // find an edge between this node and the return node of the callee function
                                auto findEdge = make_shared<UnconditionalEdge>(0, snk, succNode);
                                if (graph.find(findEdge))
                                {
                                    if( auto ue = dynamic_pointer_cast<UnconditionalEdge>(graph.getOriginalEdge(findEdge)) )
                                    {
                                        newCall->rets.dynamicRets.insert(ue);
                                        // finding at least one edge to the succNode confirms the succNode as a dynamic exit 
                                        newCall->rets.dynamicExits.insert(succNode);
                                    }
                                }
                                else
                                {
                                    // this can mean one of two things:
                                    // 1. the edge was exercised dynamically and was not captured by the profile (highly unlikely)
                                    // 2. the edge was not exercised dynamically, likely due to a select case, or invoke inst that never went to its unwind dest
                                    // We can't detect the first case with any certainty here, so just continue
                                    spdlog::warn("Found a static function exit that was not explained by the dynamic profile.");
                                }
                            }
                            // else the block is dead
                        }
                    }
                    // else this exit was probably dead code
                    else
                    {
                        spdlog::warn("Found a potential function return edge that was dead");
                    }
                }
                findUnconventionalExits(newCall);

                srcNode->removeSuccessor(origEdge);
                snkNode->removePredecessor(origEdge);
                graph.removeEdge(origEdge);
                srcNode->addSuccessor(newCall);
                snkNode->addPredecessor(newCall);
                graph.addEdge(newCall);
                newCall->setWeight((uint64_t)(origEdge->getWeight() * (float)(origEdge->getFreq())));

                // Step 3: return edge
                TransformDynamicReturnEdges(newCall, graph);
            }
            else
            {
#ifdef DEBUG
                spdlog::warn("Could not map call instruction to a profile edge: ");
                PrintVal(call);
                spdlog::warn("From Basic Block:");
                PrintVal(BB);
#endif
            }
        } // for each sink node
        // now normalize the outgoing edges of the src node
        uint64_t sum = 0;
        for (const auto &succ : srcNode->getSuccessors())
        {
            sum += succ->getFreq();
        }
        for (const auto &succ : srcNode->getSuccessors())
        {
            if (auto ce = dynamic_pointer_cast<ConditionalEdge>(succ))
            {
                ce->setWeight(sum);
            }
        }
    }

    /*
    // there is a special case where an empty function takes in a non-empty function as argument
    // in this case we will see a call instruction to an empty function, but the function argument will be called somewhere in the black box
    // the dynamic edge leading from this block will likely point to that function argument, so we have to look for them
    for( auto call : calls )
    {
        auto BB = call->getParent();
        auto BBnode = static_pointer_cast<ControlNode>(BlockToNode(graph, BB, NIDMap));
        shared_ptr<ControlNode> srcNode = nullptr;
        set<shared_ptr<ControlNode>, p_GNCompare> snkNodes;
        // we attempt to find an edge in the graph that represents this function call
        // we should have a direct mapping between this caller basic block and the entrance block of the function
        srcNode = static_pointer_cast<ControlNode>(BlockToNode(graph, BB, NIDMap));
        if( srcNode == nullptr )
        {
            continue;
        }
        set<Function*> functions;
        // if we can statically determine the callee we can just pick it
        if( call->getCalledFunction() )
        {
            if( call->getCalledFunction()->empty() )
            {
                functions.insert(call->getCalledFunction());
            }
        }
        else
        {
            set<shared_ptr<ControlNode>, p_GNCompare> targets;
            resolveNullFunctionCall(srcNode, targets, call, graph, blockCallers, IDToBlock);
            for( const auto& node : targets )
            {
                auto block = NodeToBlock(node, IDToBlock);
                if( block )
                {
                    if( block->getParent()->empty() )
                    {
                        functions.insert(block->getParent());
                    }
                }
            }
        }
        for( auto f : functions )
        {
            if( f->empty() )
            {
                // we need to look through its arguments to find out if we have a non-empty function that may be called inside the black box
                // functions can hide inside ConstantExprs, so this method needs to recursively find stuff
                deque<User*> Q;
                set<User*> covered;
                set<Function*> fpArgs;
                Q.push_front(call);
                covered.insert(call);
                while( !Q.empty() )
                {
                    for( unsigned i = 0; i < Q.front()->getNumOperands(); i++ )
                    {
                        if( auto op = dyn_cast<User>(Q.front()->getOperand(i)) )
                        {
                            if( covered.find(op) == covered.end() )
                            {
                                Q.push_back(op);
                                covered.insert(op);
                            }
                            for( unsigned j = 0; j < op->getNumOperands(); j++ )
                            {
                                if( auto f = dyn_cast<Function>(op->getOperand(j)) )
                                {
                                    if( !f->empty() )
                                    {
                                        fpArgs.insert(f);
                                    }
                                }
                            }
                        }
                    }
                    Q.pop_front();
                }
                if( fpArgs.size() > 1 )
                {
                    // 
                    throw AtlasException("Cannot yet handle multiple function pointers to an empty function!");
                }
                for( const auto& f : fpArgs )
                {
                    // each one of these inserts is a function that we could possibly go to inside the black box
                    snkNodes.insert( static_pointer_cast<ControlNode>(BlockToNode(graph, cast<BasicBlock>(f->begin()), NIDMap)) );
                }
            } // if function is nonempty
        } // for function in possible functions to call from this callbase
        // now that we have the possible destinations inside the black box, we need to carry out two steps
        // 1. make the edge going to the function arg a calledge (which means populating the ret structure within that calledge)
        // 2. make the edge goin from the function arg ret a returnedge
        for( auto snkNode : snkNodes )
        {
            // now we upgrade the edge
            auto finderEdge = make_shared<UnconditionalEdge>(0, srcNode, snkNode);
            if( graph.find(finderEdge) )
            {
                auto origEdge = graph.getOriginalEdge(finderEdge);
                auto newCall  = make_shared<CallEdge>(*origEdge);
                newCall->rets.callerNode = BBnode;
                auto functionBlock = NodeToBlock(snkNode, IDToBlock);
                newCall->rets.f = functionBlock->getParent();
                buildFunctionSubgraph(newCall, graph, blockCallers, IDToBlock, functionBlock);

                auto firstFunctionBlock = NodeToBlock(newCall->getWeightedSnk(), IDToBlock);
                set<llvm::ReturnInst*> exits;
                for( auto block = firstFunctionBlock->getParent()->begin(); block != firstFunctionBlock->getParent()->end(); block++ )
                {
                    if( auto ret = llvm::dyn_cast<ReturnInst>(block->getTerminator()) )
                    {
                        exits.insert(ret);
                    }
                }
                // for each basic block with a ret instruction, find its dynamic node, and build out the dynamic equivalents of the return edge
                for( auto& exit : exits )
                {
                    auto snk = static_pointer_cast<ControlNode>(BlockToNode(graph, exit->getParent(), NIDMap));
                    if( snk )
                    {
                        // static information for the calledge, mapped to entities in the dynamic graph
                        newCall->rets.staticExits.insert(snk);
                        newCall->rets.staticRets.insert( make_shared<UnconditionalEdge>(newCall->getFreq(), snk, BBnode) );
                        // since the function was called inside a black box, there are no static structures that will give us the successor basic blocks of the function arg call
                        // we have to use the dynamic profile to find the right edges to turn into return edges
                        for( const auto& succ : snk->getSuccessors() )
                        {
                            newCall->rets.dynamicRets.insert( succ );
                        }
                    }
                    // else this exit was probably dead code
                    else
                    {
                        spdlog::warn("Found a potential function return edge that was dead");
                    }
                }
                // it is also possible for a function to exit through something that is not a return inst (like a call to libc exit())
                // this loop looks for edges that leave the function subgraph and determines if they should also by added to the rets structure
                findUnconventionalExits(newCall);

                srcNode->removeSuccessor(origEdge);
                snkNode->removePredecessor(origEdge);
                graph.removeEdge(origEdge);
                srcNode->addSuccessor(newCall);
                snkNode->addPredecessor(newCall);
                graph.addEdge(newCall);
                newCall->setWeight((uint64_t)(origEdge->getWeight() * (float)(origEdge->getFreq())));

                // Step 3: return edge
                // return edges are nuanced in the fact that they don't return to the caller basic block, they return to a successor of the caller basic block
                TransformDynamicReturnEdges(newCall, graph);
            }
            else
            {
#ifdef DEBUG
                spdlog::warn("Could not map call instruction to a profile edge: ");
                PrintVal(call);
                spdlog::warn("From Basic Block:");
                PrintVal(BB);
#endif
            }
        } // for each sink node
    } // for call in calls
    */
    /// This profile should pass all checks now
}

/// @brief Dead functions can call live functions, and this method finds those call edges in the dynamic graph and upgrades them
///
/// Dead functions are functions that are dynamically linked into the program as an ELF object, this means they are not defined in the LLVM IR module and don't get profiled.
/// When dead functions accept function pointers, those pointers may point to a live function. When they are called inside the dead function, the profile will collect their state changes
/// This method looks through all live functions in the bitcode and repairs their incoming edges to call edges, because these call edges will be invisible when evaluating the incoming LLVM bitcode
/// @param dynamicCG    Dynamic callgraph generated from getDynamicCallGraph(). This argument will have all information discovered injected into itself, thus the argument is not const
/// @param graph        Dynamic controlgraph imported from the input profile. This argument may have edges upgraded, thus the argument is not const
void PatchFunctionEdges(const llvm::CallGraph &staticCG, Cyclebite::Graph::Graph &graph, const std::map<int64_t, vector<int64_t>> &blockCallers, const map<int64_t, llvm::BasicBlock *> &IDToBlock)
{
    for (auto node = staticCG.begin(); node != staticCG.end(); node++)
    {
        if (node->second->getFunction())
        {
            if (!node->second->getFunction()->empty())
            {
                if (string(node->second->getFunction()->getName()) == "main")
                {
                    continue;
                }
                auto firstBlock = llvm::cast<llvm::BasicBlock>(node->second->getFunction()->begin());
                auto firstNode = static_pointer_cast<ControlNode>(BlockToNode(graph, firstBlock, NIDMap));
                if (firstNode)
                {
                    auto predCopy = firstNode->getPredecessors();
                    for (auto &pred : predCopy)
                    {
                        auto callerBlock = NodeToBlock(pred->getWeightedSrc(), IDToBlock);
                        // each predecessor to a function entry node should be a calledge
                        if (auto ce = dynamic_pointer_cast<CallEdge>(pred))
                        {
                            // the calledge is already there. Make sure the ret structure has the callerBlock in its information
                            if( ce->rets.callerNode != pred->getWeightedSrc() )
                            {
                                throw AtlasException("Call edge did not have the correct callerNode!");
                            }
                        }
                        else
                        {
                            spdlog::info("Transforming calledge from function " + string(callerBlock->getParent()->getName()) + " to function " + string(node->second->getFunction()->getName()) + ".");
                            auto newCall = make_shared<CallEdge>(pred->getFreq(), pred->getWeightedSrc(), pred->getWeightedSnk());
                            newCall->rets.callerNode = pred->getWeightedSrc();
                            newCall->rets.f = node->second->getFunction();
                            buildFunctionSubgraph(newCall, graph, blockCallers, IDToBlock, firstBlock);
                            set<const llvm::Instruction *> exits;
                            for (auto block = firstBlock->getParent()->begin(); block != firstBlock->getParent()->end(); block++)
                            {
                                if (auto ret = llvm::dyn_cast<ReturnInst>(block->getTerminator()))
                                {
                                    exits.insert(ret);
                                }
                                else if( auto inv = llvm::dyn_cast<InvokeInst>(block->getTerminator()) )
                                {

                                }
                                else if( auto callb = llvm::dyn_cast<CallBrInst>(block->getTerminator()) )
                                {
                                    throw AtlasException("Cannot handle callbr instruction terminators!");
                                }
                                else if( auto res = llvm::dyn_cast<ResumeInst>(block->getTerminator()) )
                                {
                                    // resume instructions return the control flow to the calling invoke inst
                                    // ie a resume instruction is the instruction that tells the calling invoke inst to go to the unwind destination
                                    // whereas if a ret instruction gets control back to the invoke, the normal destination is taken
                                    exits.insert(res);
                                }
                                else if( auto catchsw = llvm::dyn_cast<CatchSwitchInst>(block->getTerminator()) )
                                {
                                    
                                }
                                else if( auto catchret = llvm::dyn_cast<CatchReturnInst>(block->getTerminator()) )
                                {
                                    
                                }
                                else if( auto cleanret = llvm::dyn_cast<CleanupReturnInst>(block->getTerminator()) )
                                {
                                    
                                }
                                else if( auto ur = llvm::dyn_cast<UnreachableInst>(block->getTerminator()) )
                                {
                                    
                                }
                                else
                                {
                                    // it is a terminator we do not care about
                                }
                            }
                            // for each basic block with a ret instruction, find its dynamic node, and build out the dynamic equivalents of the return edge
                            for (auto &exit : exits)
                            {
                                auto snk = static_pointer_cast<ControlNode>(BlockToNode(graph, exit->getParent(), NIDMap));
                                if (snk)
                                {
                                    // static information for the calledge, mapped to entities in the dynamic graph
                                    newCall->rets.staticExits.insert(snk);
                                    newCall->rets.staticRets.insert(make_shared<UnconditionalEdge>(newCall->getFreq(), snk, pred->getWeightedSrc()));
                                    // since the function was called inside a black box, there are no static structures that will give us the successor basic blocks of the function arg call
                                    // we have to use the dynamic profile to find the right edges to turn into return edges
                                    for( unsigned i = 0; i < callerBlock->getTerminator()->getNumSuccessors(); i++ )
                                    {
                                        auto succNode = BlockToNode(graph, callerBlock->getTerminator()->getSuccessor(i), NIDMap);
                                        if( succNode )
                                        {
                                            auto finderEdge = make_shared<UnconditionalEdge>(snk, succNode);
                                            if( graph.find(finderEdge) )
                                            {
                                                if( auto ue = dynamic_pointer_cast<UnconditionalEdge>(graph.getOriginalEdge(finderEdge)) )
                                                {
                                                    newCall->rets.dynamicExits.insert(snk);
                                                    newCall->rets.dynamicRets.insert(ue);
                                                }
                                            }
                                            else
                                            {
                                                spdlog::warn("Found a dead exit edge for a function edge patch");
                                            }
                                        }
                                        // else check if the block is dead
                                    }
                                }
                                // else this exit was probably dead code
                                else
                                {
                                    spdlog::warn("Found a potential function return edge that was dead");
                                }
                            }
                            // it is also possible for a function to exit through something that is not a return inst (like a call to libc exit())
                            // this loop looks for edges that leave the function subgraph and determines if they should also by added to the rets structure
                            findUnconventionalExits(newCall);
                            pred->getSrc()->removeSuccessor(pred);
                            pred->getSnk()->removePredecessor(pred);
                            graph.removeEdge(pred);
                            pred->getSrc()->addSuccessor(newCall);
                            pred->getSnk()->addPredecessor(newCall);
                            graph.addEdge(newCall);
                            newCall->setWeight((uint64_t)((float)pred->getFreq() / pred->getWeight()));

                            // Step 3: return edge
                            // return edges are nuanced in the fact that they don't return to the caller basic block, they return to a successor of the caller basic block
                            TransformDynamicReturnEdges(newCall, graph);
                        } // if !calledge
                    } // for each pred of function entry
                } // if function is live
            } // if not empty
        } // if not a null function
    } // for each static function
}

/// @brief Deletes fake call edges that appeared to be real in the dynamic profile because of dead functions
///
/// When a function that is empty calls a function that is live, the profile doesn't see the real call to that function.
/// When said dead function calls said live function multiple times in a row without returning, it appears to the profiler as though the function is calling itself, tail-to-head
/// These edges need to be removed because they will propogate through the analysis and screw something up later on
/// In the future, these edges may be replaced by imaginary edges that try to model what actually happened in the code, but for now they are just getting deleted
void RemoveTailHeadCalls( Cyclebite::Graph::ControlGraph& cg, const Cyclebite::Graph::CallGraph& dynamicCG, const std::map<int64_t, llvm::BasicBlock*>& IDToBlock )
{
    // set of edges that should be removed from the input control graph (because they are caused by blind spots in the dynamic profile)
    set<shared_ptr<CallEdge>, GECompare> toRemove;
    // for each call edge in the dynamic graph, we are going to see if it is actually "backed" by a call edge in the call graph
    for (const auto &edge : cg.edges())
    {
        if (auto callEdge = dynamic_pointer_cast<CallEdge>(edge))
        {
            auto srcBlock = NodeToBlock(callEdge->getWeightedSrc(), IDToBlock);
            auto snkBlock = NodeToBlock(callEdge->getWeightedSnk(), IDToBlock);
            if (srcBlock && snkBlock)
            {
                auto CGN = dynamicCG[srcBlock->getParent()];
                if( !hasDirectRecursion(dynamicCG, CGN) && !hasIndirectRecursion(dynamicCG, CGN) )
                {
                    // this is a non-recursive function call, check to see if this is the tail-head call we are looking for
                    // this case arises from an empty function calling a comparator functions (examples: FFTW -> fftwf_dimcmp, C++ -> any STL container with a specialized comparator ie operator()( const T& lhs, const T& rhs ) )
                    // we can check this case by confirming that 
                    // 1. the src block of the call instruction contains some kind of function return
                    // 2. the snk block of the call is the first block in the function
                    auto isTheCase = false;
                    for( auto i = srcBlock->begin(); i != srcBlock->end(); i++ )
                    {
                        if( auto ret = llvm::dyn_cast<ReturnInst>(i) )
                        {
                            isTheCase = true;
                        }
                        else if( auto resume = llvm::dyn_cast<ResumeInst>(i) )
                        {
                            isTheCase = true;
                        }
                    }
                    if( snkBlock != llvm::cast<BasicBlock>(srcBlock->getParent()->begin()) )
                    {
                        isTheCase = false;
                    }
                    if( isTheCase )
                    {
                        // we delete the edge that shouldn't be there (the edge that went from tail to head of the function)
                        toRemove.insert(callEdge);
                    }
                } // if a non-recursive function call
            } // if src & snk BBs exist
        } // if a call edge
    } // for each dynamic edge
    // and remove the edges we collected
    for( const auto& r : toRemove )
    {
#ifdef DEBUG
        spdlog::info("Removing fake recursive call edge for function "+string(r->rets.f->getName()));
#endif
        auto src = r->getSrc();
        auto snk = r->getSnk();
        src->removeSuccessor(r);
        snk->removePredecessor(r);
        // re-weight outgoing edges
        uint64_t sum = 0;
        for( const auto& succ : src->getSuccessors() )
        {
            sum += static_pointer_cast<UnconditionalEdge>(succ)->getFreq();
        }
        for( auto& succ : src->getSuccessors() )
        {
            if( auto cond = dynamic_pointer_cast<ConditionalEdge>(succ) )
            {
                cond->setWeight(sum);
            }
        }
        cg.removeEdge(r);
    }
}

void Cyclebite::Graph::getDynamicInformation(Cyclebite::Graph::ControlGraph& cg, Cyclebite::Graph::CallGraph& dynamicCG, const std::string& filePath, const unique_ptr<llvm::Module>& SourceBitcode, const llvm::CallGraph& staticCG, const map<int64_t, vector<int64_t>>& blockCallers, const set<int64_t>& threadStarts, const map<int64_t, BasicBlock*>& IDToBlock, bool HotCodeDetection)
{
    Graph graph;
    // node that was observed to exit the program
    shared_ptr<ControlNode> terminator;
    try
    {
        auto err = BuildCFG(graph, filePath, HotCodeDetection);
        if (err)
        {
            throw AtlasException("Failed to read input profile file!");
        }
        if (graph.empty())
        {
            throw AtlasException("No nodes could be read from the input profile!");
        }
        UpgradeEdges(SourceBitcode.get(), graph, blockCallers, IDToBlock);
        PatchFunctionEdges(staticCG, graph, blockCallers, IDToBlock);
        terminator = AddImaginaryEdges(SourceBitcode.get(), graph, threadStarts);
        dynamicCG = getDynamicCallGraph(SourceBitcode.get(), graph, blockCallers, IDToBlock);
        cg = ControlGraph(graph, terminator);
        RemoveTailHeadCalls(cg, dynamicCG, IDToBlock);
    }
    catch (AtlasException &e)
    {
        spdlog::critical(e.what());
        exit(EXIT_FAILURE);
    }
#ifdef DEBUG
    ofstream DynamicCallallGraphDot("DynamicCallGraph.dot");
    auto dynamicCallGraph = GenerateCallGraph(dynamicCG);
    DynamicCallallGraphDot << dynamicCallGraph << "\n";
    DynamicCallallGraphDot.close();
    try
    {
        Checks(cg, "ProfileRead");
        CallGraphChecks(staticCG, dynamicCG, cg, IDToBlock);
    }
    catch (AtlasException &e)
    {
        spdlog::critical(e.what());
        exit(EXIT_FAILURE);
    }
#endif
}

const Cyclebite::Graph::CallGraph Cyclebite::Graph::getDynamicCallGraph(llvm::Module *mod, const Graph &graph, const std::map<int64_t, std::vector<int64_t>> &blockCallers, const std::map<int64_t, llvm::BasicBlock *> &IDToBlock)
{
    Cyclebite::Graph::CallGraph dynamicCG;
    for (auto f = mod->begin(); f != mod->end(); f++)
    {
        const llvm::Function *F = cast<Function>(f);
        // create node in the dynamic CG, if this function was ever used in the profile
        shared_ptr<Cyclebite::Graph::CallGraphNode> newNode = nullptr;
        if (!F->empty())
        {
            auto firstBlock = cast<BasicBlock>(F->begin());
            if (BlockToNode(graph, firstBlock, NIDMap))
            {
                if (!dynamicCG.find(F))
                {
                    // this function is live
                    newNode = make_shared<Cyclebite::Graph::CallGraphNode>(F);
                    dynamicCG.addNode(newNode);
                }
                else
                {
                    newNode = dynamicCG[F];
                }
            }
        }
        if (!newNode)
        {
            continue;
        }
        for (auto b = F->begin(); b != F->end(); b++)
        {
            // confirm that this block is live
            if (!BlockToNode(graph, cast<BasicBlock>(b), NIDMap))
            {
                continue;
            }
            for (auto i = b->begin(); i != b->end(); i++)
            {
                if (auto cb = dyn_cast<CallBase>(i))
                {
                    vector<const llvm::Function *> children;
                    if (cb->getCalledFunction())
                    {
                        if (!cb->getCalledFunction()->empty())
                        {
                            children.push_back(cb->getCalledFunction());
                        }
                    }
                    else
                    {
                        // try to find a block caller entry for this function, if it's not there we have to move on
                        auto BBID = GetBlockID(llvm::cast<llvm::BasicBlock>(b));
                        if (blockCallers.find(BBID) != blockCallers.end())
                        {
                            for (auto entry : blockCallers.at(BBID))
                            {
                                children.push_back(IDToBlock.at(entry)->getParent());
                            }
                        }
                    }
                    for (const auto child : children)
                    {
                        shared_ptr<Cyclebite::Graph::CallGraphNode> childNode = nullptr;
                        if (dynamicCG.find(child))
                        {
                            childNode = dynamicCG[child];
                        }
                        else
                        {
                            childNode = make_shared<Cyclebite::Graph::CallGraphNode>(child);
                            dynamicCG.addNode(childNode);
                        }
                        // this is a check to see if the edge from the call instruction to the callee function is live
                        auto callerNode = BlockToNode(graph, cast<BasicBlock>(b), NIDMap);
                        auto calleeNode = BlockToNode(graph, cast<BasicBlock>(child->begin()), NIDMap);
                        auto finderEdge = make_shared<UnconditionalEdge>(0, callerNode, calleeNode);
                        if (graph.find(finderEdge))
                        {
                            auto e = graph.getOriginalEdge(finderEdge);
                            if (auto ce = dynamic_pointer_cast<CallEdge>(e))
                            {
                                set<shared_ptr<CallEdge>, GECompare> callEdges;
                                callEdges.insert(ce);
                                if (newNode->isPredecessor(childNode))
                                {
                                    // this edge already exists between parent and child, add the CallEdge to the CallGraphEdge
                                    shared_ptr<CallGraphEdge> childEdge = nullptr;
                                    auto childrenCopy = newNode->getChildren();
                                    for (const auto &succ : childrenCopy)
                                    {
                                        if (succ->getChild() == childNode)
                                        {
                                            childEdge = succ;
                                            callEdges.insert(childEdge->getCallEdges().begin(), childEdge->getCallEdges().end());
                                            childNode->removePredecessor(childEdge);
                                            newNode->removeSuccessor(childEdge);
                                            dynamicCG.removeEdge(childEdge);
                                            break;
                                        }
                                    }
                                }
                                auto newEdge = make_shared<CallGraphEdge>(newNode, childNode, callEdges);
                                newNode->addSuccessor(newEdge);
                                childNode->addPredecessor(newEdge);
                                dynamicCG.addEdge(newEdge);
                            }
                            else
                            {
                                throw AtlasException("Edge between two functions was not a calledge!");
                            }
                        }
                        else if (calleeNode == nullptr)
                        {
                            // this is a live function that is somehow dead even though its caller block is live
                            // this can be found in OpenCV/travelingsalesman (BBID 225,613 calls BBID 52,890 but it doesn't show in the profile)
                            // since the edge is dead, we have to remove the function node from the graph and throw a warning
                            dynamicCG.removeNode(childNode);
                            spdlog::warn("Found a resolvable defined callinst whose caller is live but callee is dead.");
                        }
                        else if (calleeNode != nullptr)
                        {
                            // this can happen when two blocks who were dynamically used also have a calledge between each other in the static code
                            // since the profile only observes the program after the start of main and before the end of main, two blocks can be live but the edge between them may not be
                            // for example, when the static call edge occurs outside the boundaries of main
                            spdlog::warn("Nodes " + to_string(callerNode->NID) + " and " + to_string(calleeNode->NID) + " have a statically defined calledge that was not observed in the dynamic profile");
                            if (childNode->getPredecessors().empty() && childNode->getSuccessors().empty())
                            {
                                dynamicCG.removeNode(childNode);
                            }
                        }
                    }
                }
            }
        }
        // now cover the cases that are not expressed in the bitcode
        // this can arise if the current function is called by an empty function
        // but we need to be careful here - we don't want to inject blindspots from the dynamic profile into the dynamic call graph
        // - for example, when we get the TailToHeadCaller case (see RemoveTailHeadCalls()), we want to ignore that edge because it doesn't actually exist (it can make a non-recursive function look recursive)
        auto entryNode = BlockToNode(graph, llvm::cast<llvm::BasicBlock>(newNode->getFunction()->begin()), NIDMap);
        for (const auto &pred : entryNode->getPredecessors())
        {
            if (auto ce = dynamic_pointer_cast<CallEdge>(pred))
            {
                // check to see if we are about to make a function recursive by "filling in" this blind spot
                // if we are, we skip this function because it leads to bad results
                shared_ptr<CallGraphNode> parent = nullptr;
                auto callerBlock = NodeToBlock(ce->getWeightedSrc(), IDToBlock);
                auto calleeBlock = NodeToBlock(ce->getWeightedSnk(), IDToBlock);
                if (callerBlock && calleeBlock )
                {
                    if( callerBlock->getParent() == calleeBlock->getParent() )
                    {
                        // skip
                        continue;
                    }
                    // this will not make a recursive function call, get the function of the callerblock and map it to a node in the callgraph (or make a new one if necessary)
                    if (dynamicCG.find(callerBlock->getParent()))
                    {
                        parent = dynamicCG[callerBlock->getParent()];
                    }
                    else
                    {
                        parent = make_shared<Cyclebite::Graph::CallGraphNode>(callerBlock->getParent());
                        dynamicCG.addNode(parent);
                    }
                    if (!parent)
                    {
                        throw AtlasException("Could not map the parent node of a calledge to a node in the dynamic callgraph!");
                    }
                }
                else
                {
                    throw AtlasException("Found a dead function in the dynamic control graph!");
                }

                set<shared_ptr<CallEdge>, GECompare> callEdges;
                callEdges.insert(ce);
                if (newNode->isSuccessor(parent))
                {
                    // this edge already exists between parent and child, add the CallEdge to the CallGraphEdge
                    shared_ptr<CallGraphEdge> parentEdge = nullptr;
                    auto parentsCopy = newNode->getParents();
                    for (const auto &pred : parentsCopy)
                    {
                        if (pred->getParent() == parent)
                        {
                            parentEdge = pred;
                            callEdges.insert(parentEdge->getCallEdges().begin(), parentEdge->getCallEdges().end());
                            parent->removeSuccessor(parentEdge);
                            newNode->removePredecessor(parentEdge);
                            dynamicCG.removeEdge(parentEdge);
                            break;
                        }
                    }
                }
                auto newEdge = make_shared<CallGraphEdge>(parent, newNode, callEdges);
                newNode->addPredecessor(newEdge);
                parent->addSuccessor(newEdge);
                dynamicCG.addEdge(newEdge);
            }
        }
    }
    return dynamicCG;
}

void Cyclebite::Graph::CallGraphChecks(const llvm::CallGraph &SCG, const Cyclebite::Graph::CallGraph &DCG, const Graph &dynamicGraph, const std::map<int64_t, llvm::BasicBlock *> &IDToBlock)
{
    // do the dynamicGraph calledges and the DCG edges agree?
    for (const auto &edge : DCG.edges())
    {
        auto callEdge = static_pointer_cast<CallGraphEdge>(edge);
        for (const auto &call : callEdge->getCallEdges())
        {
            auto srcNode = call->getSrc();
            auto snkNode = call->getSnk();
            if (!dynamicGraph.find(srcNode))
            {
                throw AtlasException("Dynamic call graph contained a node whose calledge had an invalid src!");
            }
            else if (!dynamicGraph.find(snkNode))
            {
                throw AtlasException("Dynamic call graph contained a node whose calledge had an invalid snk!");
            }
            else if (dynamicGraph.find(call))
            {
                auto edge = dynamicGraph.getOriginalEdge(call);
                if (auto ce = dynamic_pointer_cast<CallEdge>(edge))
                {
                    // great we have a call edge that we should in the graph
                }
                else
                {
                    throw AtlasException("Dynamic call graph contained a calledge that was not a calledge in the dynamic graph!");
                }
            }
            else
            {
                throw AtlasException("Dynamic call graph contained a calledge that was not in the dynamic graph!");
            }
        }
    }
    // which functions are alive in the static callgraph? do they all have call edges in the dynamic graph?
    for (const auto &node : SCG)
    {
        if (node.second->getFunction())
        {
            if (!node.second->getFunction()->empty())
            {
                // we don't do this analysis for main because we won't have call edges for main
                if (string(node.second->getFunction()->getName()) != "main")
                {
                    auto firstNode = BlockToNode(dynamicGraph, llvm::cast<BasicBlock>(node.second->getFunction()->begin()), NIDMap);
                    if (firstNode)
                    {
                        // this is a live function
                        // all edges leading into this firstNode should be a calledge
                        for (const auto &pred : firstNode->getPredecessors())
                        {
                            if (auto ce = dynamic_pointer_cast<CallEdge>(pred))
                            {
                                // great
                            }
                            else if (auto ret = dynamic_pointer_cast<ReturnEdge>(pred))
                            {
                                // also great
                                // when embedded functions within dead functions get called multiple times, it appears as though they go head-to-tail
                                // this can also happen in recursion
                            }
                            else
                            {
                                if( auto ue = dynamic_pointer_cast<UnconditionalEdge>(pred) )
                                {
                                    auto predFunction = NodeToBlock(ue->getWeightedSrc(), IDToBlock);
                                    spdlog::critical("Live function " + string(node.second->getFunction()->getName()) + " has a predecessor from " + string(predFunction->getName()) + " that is not a call edge!");
                                    throw AtlasException("Live function " + string(node.second->getFunction()->getName()) + " has a predecessor from " + string(predFunction->getName()) + " that is not a call edge!");
                                }
                                else
                                {
                                    throw AtlasException("Live function " + string(node.second->getFunction()->getName()) + " has an unresolvable predecessor that is not a call edge!");
                                }                            
                            }
                        }
                    }
                }
            }
        }
    }
    // who is empty in the static callgraph? Do they have edges to non-empty functions? Have we accounted for them all in the dynamic graph?
}

shared_ptr<Inst> ConstructCallNode( const shared_ptr<Inst>& newNode, const llvm::CallBase* call, const Cyclebite::Graph::CallGraph& dynamicCG, const map<int64_t, std::shared_ptr<ControlNode>> &blockToNode, const set<std::shared_ptr<ControlBlock>, p_GNCompare> &programFlow, const std::map<int64_t, llvm::BasicBlock*>& IDToBlock )
{
    // upgrade to a CallNode
    // the llvm::CallBase instruction may contain missing information ie a function pointer
    // to fill in this information, we have to create a mapping between llvm::CallBase and Cyclebite::Graph::CallEdge
    // right now this mapping doesn't exist apriori, so we have to do it manually
    
    // vector of destinations for this call edge
    set<shared_ptr<ControlBlock>, p_GNCompare> dests;
    if( call->getCalledFunction() )
    {
        if( !call->getCalledFunction()->empty() )
        {
            // if the function call is statically resolvable and the callee is non-empty, we don't need to map anything
            // just get the destination block of the call instruction and map it to a Cyclebite::Graph::ControlBlock (by finding an existing one or making one)
            auto firstBlock = cast<llvm::BasicBlock>(call->getCalledFunction()->begin());
            shared_ptr<ControlBlock> dest = nullptr;
            auto blockID = GetBlockID(firstBlock);
            bool skip = false;
            if( blockID == IDState::Uninitialized )
            {
                // this block doesn't have an ID, which is unusual and deserves a warning
                // this can happen when an exception is never thrown, or is always thrown
                spdlog::warn("Found a block that has no ID!");
                skip = true;
            }
            else if( blockID == IDState::Artificial )
            {
                // somehow we have come across an injected function, ignore as well
                skip = true;
            }
            else if( blockToNode.find(blockID) == blockToNode.end() )
            {
                // this block is dead, skip
                skip = true;
            }
            if( !skip )
            {
                auto destIt = programFlow.find( blockID );
                if( destIt != programFlow.end() )
                {
                    dest = *destIt;
                }
                else
                {
                    // for now we instantiate this control block with an empty set of instructions
                    // later we will "upgrade" this block when its instructions are ready
                    set<shared_ptr<Inst>, p_GNCompare> insts;
                    dest = make_shared<ControlBlock>(blockToNode.at( blockID ), insts);
                }
                dests.insert( dest );
            }
        }
        else
        {
            // if the called function is empty, there is no information we can use to build out the rest of the dynamic control graph
            // thus we don't have any destinations to add
        }
    }
    else
    {
        // we are dealing with a function pointer
        // in order to get the information from the dynamic call graph to resolve this function pointer, we need to map this llvm::CallBase to a Cyclebite::Graph::CallEdge
        // we do this using the dynamic call graph, which contains all information that was included at compile time about the call graph (thus, all function pointers that point to live functions are resolved)
        // specifically we compare nodes on the src side of all possible Cyclebite::Graph::CallEdge's that could be representing the llvm::CallBase "call" (there is an ID mapping between llvm::BasicBlock* and Cyclebite::Graph::ControlNode*)
        // since our bitcode format pass (Util/include/Format.h) allows only one call instruction per basic block, we can be guaranteed that any Cyclebite::Graph::CallEdge originating from our src node represents "call"
        // everything that is dead will be discovered here as unresolvable, so the fail conditions will be caused by dead functions

        // we find the function that the llvm::Instruction "call" belongs to in the Cyclebite::Graph::CallGraph (which contains all dynamic information)
        if( dynamicCG.find(call->getParent()->getParent()) )
        {
            // first we must locate the Cyclebite::Graph::CallGraphNode that corresponds to the parent function of the llvm::CallBase "call"
            // this will get us all function call edges that lead from call's parent to its children, and zero or one or more of those edges may represent "call"
            // (if "call" points to a dead function, we won't find a representing edge for it. If "call" pointed to one or more functions during execution, we will get all those functions)

            // set of all Cyclebite::Graph::CallEdge's that could represent the llvm::CallBase "call"
            set<shared_ptr<CallEdge>, GECompare> representatives;
            for( const auto& child : dynamicCG[call->getParent()->getParent()]->getChildren() )
            {
                // we search through the Cyclebite::Graph::CallEdge's of this Cyclebite::Graph::CallGraphEdge to find a matchup between a Cyclebite::Graph::CallEdge and the llvm::CallInstruction ie "call"
                // since we only allow for one call instruction in a given basic block (via Util/Format.h), we can be sure that all Cyclebite::Graph::CallEdge that have our basic block as src are representing "call"
                shared_ptr<Cyclebite::Graph::CallEdge> found = nullptr;
                for( auto ce : child->getCallEdges() )
                {
                    auto src = ce->getWeightedSrc();
                    auto blockID = GetBlockID(call->getParent());
                    auto blockNode = blockToNode.at(blockID);
                    if( src == blockNode )
                    {
                        representatives.insert(ce);
                    }
                }
            }
            for( const auto& r : representatives )
            {
                // get the llvm::Function* that represented the snk node of this call edge
                auto parent = NodeToBlock(r->getWeightedSnk(), IDToBlock)->getParent();
                if( !parent->empty() )
                {
                    auto firstBlock = llvm::cast<llvm::BasicBlock>(parent->begin());
                    shared_ptr<ControlBlock> dest = nullptr;
                    auto destIt = programFlow.find( GetBlockID(firstBlock) );
                    if( destIt != programFlow.end() )
                    {
                        dest = *destIt;
                    }
                    else
                    {
                        // for now we instantiate this control block with an empty set of instructions
                        // later we will "upgrade" this block when its instructions are ready
                        set<shared_ptr<Inst>, p_GNCompare> insts;
                        dest = make_shared<ControlBlock>(blockToNode.at(GetBlockID(firstBlock)), insts);
                    }
                    dests.insert( dest );
                }
                else
                {
                    // this empty function is unresolvable
                    // shouldn't happen though, because an entry in the IDToBlock map should belong to a non-empty function
                    throw AtlasException("Found a block in IDToBlock whose parent is empty!");
                }

            } // for r in representatives
        }
        else
        {
            // something is wrong, this live function should be in the dynamicCG
            throw AtlasException("Could not find live function in the dynamicCG!");
        }
    }
    // do the upgrading
    return make_shared<Cyclebite::Graph::CallNode>(newNode.get(), dests);
}

int Cyclebite::Graph::BuildDFG(llvm::Module *SourceBitcode, const Cyclebite::Graph::CallGraph& dynamicCG, map<int64_t, std::shared_ptr<ControlNode>> &blockToNode, set<std::shared_ptr<ControlBlock>, p_GNCompare> &programFlow, DataGraph &graph, std::map<std::string, set<int64_t>> &specialInstructions, const std::map<int64_t, llvm::BasicBlock*>& IDToBlock)
{
    set<int64_t> inductionVariables;
    set<int64_t> basePointers;
    set<int64_t> kernelFunctions;
    if (specialInstructions.find("IV") != specialInstructions.end())
    {
        for (auto id : specialInstructions.at("IV"))
        {
            inductionVariables.insert(id);
        }
    }
    if (specialInstructions.find("BP") != specialInstructions.end())
    {
        for (auto id : specialInstructions.at("BP"))
        {
            basePointers.insert(id);
        }
    }
    if (specialInstructions.find("KF") != specialInstructions.end())
    {
        for (auto id : specialInstructions.at("KF"))
        {
            kernelFunctions.insert(id);
        }
    }
    // this section constructs the data flow of instructions (Cyclebite::Graph::Inst) and Cyclebite::Graph::ControlBlock
    for (auto f = SourceBitcode->begin(); f != SourceBitcode->end(); f++)
    {
        for (auto bit = f->begin(); bit != f->end(); bit++)
        {
            auto blockID = GetBlockID(cast<BasicBlock>(bit));
            if (blockToNode.find(blockID) == blockToNode.end())
            {
                //throw AtlasException("Cannot map a basic block to a ControlNode!");
                spdlog::warn("Cannot map a basic block to a ControlNode!");
                continue;
            }
            // these instructions will be passed into a ControlBlock at the end
            set<std::shared_ptr<Inst>, p_GNCompare> blockInstructions;
            for (auto it = bit->begin(); it != bit->end(); it++)
            {
                const auto inst = cast<Instruction>(it);
                std::shared_ptr<Inst> newNode = nullptr;
                if (DNIDMap.find(inst) == DNIDMap.end())
                {
                    // checks to see if this is a base pointer or induction variablej
                    auto ID = GetValueID(inst);
                    if (inductionVariables.find(ID) != inductionVariables.end())
                    {
                        newNode = make_shared<Inst>(inst, DNC::State);
                    }
                    else if (basePointers.find(ID) != basePointers.end())
                    {
                        newNode = make_shared<Inst>(inst, DNC::Memory);
                    }
                    else if (kernelFunctions.find(ID) != kernelFunctions.end())
                    {
                        newNode = make_shared<Inst>(inst, DNC::Function);
                    }
                    else
                    {
                        newNode = make_shared<Inst>(inst);
                    }
                    if( auto call = dyn_cast<CallBase>(inst) )
                    {
                        newNode = ConstructCallNode(newNode, call, dynamicCG, blockToNode, programFlow, IDToBlock);
                    }
                    newNode->op = GetOp(inst->getOpcode());
                    graph.addNode(newNode);
                }
                else
                {
                    newNode = static_pointer_cast<Inst>(DNIDMap[inst]);
                    if( auto call = llvm::dyn_cast<llvm::CallBase>(inst) )
                    {
                        graph.removeNode(newNode);
                        newNode = ConstructCallNode(newNode, call, dynamicCG, blockToNode, programFlow, IDToBlock);
                        graph.addNode(newNode);
                    }
                }
                DNIDMap.insert(pair<const llvm::Instruction*, const shared_ptr<Inst>>(inst, newNode));
                blockInstructions.insert(newNode);
                for (const auto& use : inst->users())
                {
                    if (const auto& user = dyn_cast<Instruction>(use))
                    {
                        shared_ptr<Inst> neighborNode = nullptr;
                        if (DNIDMap.find(user) != DNIDMap.end())
                        {
                            neighborNode = static_pointer_cast<Inst>(DNIDMap[user]);
                        }
                        else
                        {
                            // checks to see if this is a base pointer or induction variablej
                            auto ID = GetValueID(user);
                            if (inductionVariables.find(ID) != inductionVariables.end())
                            {
                                neighborNode = make_shared<Inst>(user, DNC::State);
                            }
                            else if (basePointers.find(ID) != basePointers.end())
                            {
                                neighborNode = make_shared<Inst>(user, DNC::Memory);
                            }
                            else if (kernelFunctions.find(ID) != kernelFunctions.end())
                            {
                                neighborNode = make_shared<Inst>(user, DNC::Function);
                            }
                            else
                            {
                                neighborNode = make_shared<Inst>(user);
                            }
                            neighborNode->op = GetOp(user->getOpcode());
                            graph.addNode(neighborNode);
                        }
                        // we have a user and we need to find a direct mapping between this instruction and that user
                        // in order for the mapping to be direct (ie directly inferrable from the input profile) the user instruction must be MARKOV_ORDER basic blocks or less away from this one
                        // right now we are not going to deal with this
                        auto newEdge = make_shared<UnconditionalEdge>(newNode, neighborNode);
                        if (!graph.find(newEdge))
                        {
                            graph.addEdge(newEdge);
                        }
                        else
                        {
                            auto staleEdge = newEdge;
                            newEdge = static_pointer_cast<UnconditionalEdge>(graph.getOriginalEdge(staleEdge));
                        }
                        newNode->addSuccessor(newEdge);
                        neighborNode->addPredecessor(newEdge);
                        DNIDMap.insert(pair<const llvm::Instruction*, const shared_ptr<Inst>>(user, neighborNode));
                    }
                }
                for (const auto &val : inst->operands())
                {
                    if (const auto& predInst = dyn_cast<Instruction>(val))
                    {
                        std::shared_ptr<Inst> nodePred = nullptr;
                        if (DNIDMap.find(predInst) != DNIDMap.end())
                        {
                            nodePred = static_pointer_cast<Inst>(DNIDMap[predInst]);
                        }
                        else
                        {
                            // checks to see if this is a base pointer or induction variable
                            auto ID = GetValueID(predInst);
                            if (inductionVariables.find(ID) != inductionVariables.end())
                            {
                                nodePred = make_shared<Inst>(predInst, DNC::State);
                            }
                            else if (basePointers.find(ID) != basePointers.end())
                            {
                                nodePred = make_shared<Inst>(predInst, DNC::Memory);
                            }
                            else if (kernelFunctions.find(ID) != kernelFunctions.end())
                            {
                                nodePred = make_shared<Inst>(predInst, DNC::Function);
                            }
                            else
                            {
                                nodePred = make_shared<Inst>(predInst);
                            }
                            nodePred->op = GetOp(predInst->getOpcode());
                            DNIDMap.insert(pair<const llvm::Instruction*, const shared_ptr<Inst>>(predInst, nodePred));
                            graph.addNode(nodePred);
                        }
                        auto newEdge = make_shared<UnconditionalEdge>(nodePred, newNode);
                        if (!graph.find(newEdge))
                        {
                            graph.addEdge(newEdge);
                        }
                        else
                        {
                            auto staleEdge = newEdge;
                            newEdge = static_pointer_cast<UnconditionalEdge>(graph.getOriginalEdge(staleEdge));
                        }
                        newNode->addPredecessor(newEdge);
                        nodePred->addSuccessor(newEdge);
                    }
                    else if( const auto& arg = dyn_cast<Argument>(val) )
                    {
                        std::shared_ptr<DataValue> argNode = nullptr;
                        if( DNIDMap.find(arg) != DNIDMap.end() )
                        {
                            argNode = DNIDMap.at(arg);
                        }
                        else
                        {
                            argNode = make_shared<DataValue>(arg);
                            DNIDMap.insert(pair<const llvm::Value*, const shared_ptr<DataValue>>(arg, argNode));
                            graph.addNode(argNode);
                        }
                        auto newEdge = make_shared<UnconditionalEdge>(argNode, newNode);
                        if (!graph.find(newEdge))
                        {
                            graph.addEdge(newEdge);
                        }
                        else
                        {
                            auto staleEdge = newEdge;
                            newEdge = static_pointer_cast<UnconditionalEdge>(graph.getOriginalEdge(staleEdge));
                        }
                        newNode->addPredecessor(newEdge);
                        argNode->addSuccessor(newEdge);
                    }
                    // you can only communicate with globals via ld/st in LLVM IR
                    // thus, all we have to do is look at instruction operands to find their uses (ie an instruction cannot be used directly by a global)
                    /*else if( const auto& glob = dyn_cast<GlobalValue>(val) )
                    {
                        std::shared_ptr<Global> globalNode = nullptr;
                        if( DNIDMap.find(glob) != DNIDMap.end() )
                        {
                            globalNode = static_pointer_cast<Global>(DNIDMap.at(glob));
                        }
                        else
                        {
                            globalNode = make_shared<Global>(glob);
                            DNIDMap.insert(pair<const llvm::GlobalValue*, const shared_ptr<Global>>(glob, globalNode));
                            graph.addNode(globalNode);
                        }
                        auto newEdge = make_shared<UnconditionalEdge>(globalNode, newNode);
                        if (!graph.find(newEdge))
                        {
                            graph.addEdge(newEdge);
                        }
                        else
                        {
                            auto staleEdge = newEdge;
                            newEdge = static_pointer_cast<UnconditionalEdge>(graph.getOriginalEdge(staleEdge));
                        }
                        newNode->addPredecessor(newEdge);
                        globalNode->addSuccessor(newEdge);
                    }*/
                }
            }
            // there is a one-to-one mapping between llvm::BasicBlock and Cyclebite::Graph::ControlBlock
            shared_ptr<Cyclebite::Graph::ControlBlock> newBBsub = nullptr;
            if( programFlow.find(blockToNode[blockID]) != programFlow.end() )
            {
                // this condition means a call instruction pointed to this block before this ControlBlock's instructions were ready
                newBBsub = *programFlow.find(blockToNode[blockID]);
                for( const auto& inst : blockInstructions )
                {
                    newBBsub->instructions.insert(inst);
                }
            }
            else
            {
                // this bb hasn't been constructed yet, so we construct it with its instructions
                newBBsub = make_shared<ControlBlock>(blockToNode[blockID], blockInstructions);
            }
            for (auto inst : blockInstructions)
            {
                inst->parent = newBBsub;
            }
            programFlow.insert(newBBsub);
            BBCBMap.insert(pair<const llvm::BasicBlock*, const shared_ptr<ControlBlock>>(cast<BasicBlock>(bit), newBBsub));
            blockInstructions.clear();
        }
    }
    return EXIT_SUCCESS;
}

void ProfileBlock(BasicBlock *BB, map<int64_t, map<string, uint64_t>> &rMap, map<int64_t, map<string, uint64_t>> &cpMap)
{
    int64_t id = GetBlockID(BB);
    for (auto bi = BB->begin(); bi != BB->end(); bi++)
    {
        auto *i = cast<Instruction>(bi);
        if (i->getMetadata("TikSynthetic") != nullptr)
        {
            continue;
        }
        // opcode
        string name = string(i->getOpcodeName());
        rMap[id][name + "Count"]++;
        // type
        Type *t = i->getType();
        if (t->isVoidTy())
        {
            rMap[id]["typeVoid"]++;
            cpMap[id][name + "typeVoid"]++;
        }
        else if (t->isFloatingPointTy())
        {
            rMap[id]["typeFloat"]++;
            cpMap[id][name + "typeFloat"]++;
        }
        else if (t->isIntegerTy())
        {
            rMap[id]["typeInt"]++;
            cpMap[id][name + "typeInt"]++;
        }
        else if (t->isArrayTy())
        {
            rMap[id]["typeArray"]++;
            cpMap[id][name + "typeArray"]++;
        }
        else if (t->isVectorTy())
        {
            rMap[id]["typeVector"]++;
            cpMap[id][name + "typeVector"]++;
        }
        else if (t->isPointerTy())
        {
            rMap[id]["typePointer"]++;
            cpMap[id][name + "typePointer"]++;
        }
        else
        {
            std::string str;
            llvm::raw_string_ostream rso(str);
            t->print(rso);
            spdlog::warn("Unrecognized type: " + str);
        }
        rMap[id]["instructionCount"]++;
        cpMap[id]["instructionCount"]++;
    }
}

map<string, map<string, map<string, int>>> Cyclebite::Graph::ProfileKernels(const std::map<string, std::set<int64_t>> &kernels, Module *M, const std::map<int64_t, uint64_t> &blockCounts)
{
    map<int64_t, map<string, uint64_t>> rMap;  //dictionary which keeps track of the actual information per block
    map<int64_t, map<string, uint64_t>> cpMap; //dictionary which keeps track of the cross product information per block
    //start by profiling every basic block
    for (auto &F : *M)
    {
        for (Function::iterator BB = F.begin(), E = F.end(); BB != E; ++BB)
        {
            ProfileBlock(cast<BasicBlock>(BB), rMap, cpMap);
        }
    }

    // maps kernel ID to type of pi to instruction type to instruction count
    map<string, map<string, map<string, int>>> fin;

    map<string, map<string, int>> cPigData;  //from the trace
    map<string, map<string, int>> pigData;   //from the bitcode
    map<string, map<string, int>> ecPigData; //cross product from the trace
    map<string, map<string, int>> epigData;  //cross product from the bitcode

    for (const auto &kernel : kernels)
    {
        string iString = kernel.first;
        auto blocks = kernel.second;
        for (auto block : blocks)
        {
            // frequency count of this block
            uint64_t count;
            if (blockCounts.find(block) == blockCounts.end())
            {
                count = 0;
            }
            else
            {
                count = blockCounts.at(block);
            }
            for (const auto &pair : rMap[block])
            {
                cPigData[iString][pair.first] += pair.second * count;
                pigData[iString][pair.first] += pair.second;
            }

            for (const auto &pair : cpMap[block])
            {
                ecPigData[iString][pair.first] += pair.second * count;
                epigData[iString][pair.first] += pair.second;
            }
        }
    }

    // now do kernel ID wise mapping
    for (const auto &kernelID : pigData)
    {
        fin[kernelID.first]["Pig"] = kernelID.second;
    }
    for (const auto &kernelID : cPigData)
    {
        fin[kernelID.first]["CPig"] = kernelID.second;
    }
    for (const auto &kernelID : epigData)
    {
        fin[kernelID.first]["EPig"] = kernelID.second;
    }
    for (const auto &kernelID : ecPigData)
    {
        fin[kernelID.first]["ECPig"] = kernelID.second;
    }
    return fin;
}

string Cyclebite::Graph::GenerateDot(const Graph &graph, bool original)
{
    string dotString = "digraph{\n";
    // label imaginary nodes and kernels
    int mappedKID = 0;
    for( const auto& node : graph.nodes() )
    {
        if( auto in = dynamic_pointer_cast<ImaginaryNode>(node) )
        {
            dotString += "\t" + to_string(in->NID) + " [label=VOID];\n";
        }
        else if( auto mlc = dynamic_pointer_cast<MLCycle>(node) )
        {
            string label = to_string(mappedKID++);
            if( !mlc->Label.empty() )
            {
                label = mlc->Label;
            }
            dotString += "\t" + to_string(mlc->NID) + " [label=\"" + label + "\", color=blue];\n";
        }
    }

    // label nodes based on their node IDs
    // this makes generating segmented plots in inkscape much easier
    if (original)
    {
        for (const auto &node : graph.nodes() )
        {
            string origBlocks = "";
            if( auto controlNode = dynamic_pointer_cast<ControlNode>(node) )
            {
                if (controlNode->originalBlocks.empty())
                {
                    continue;
                }
                auto block = controlNode->originalBlocks.rbegin();
                origBlocks += to_string(*block);
                if (markovOrder > 1 && (controlNode->originalBlocks.size() > 1))
                {
                    // this code generates a probability-inspired notation to represent the original blocks that represented this possibly-multi-dimensional node
                    origBlocks += "|";
                    block++;
                    origBlocks += to_string(*block);
                    block++;
                    for (; block != controlNode->originalBlocks.rend(); block++)
                    {
                        origBlocks += "," + to_string(*block);
                    }
                }
            }
            else if( auto in = dynamic_pointer_cast<ImaginaryNode>(node) )
            {
                origBlocks = "VOID";
            }
            dotString += "\t" + to_string(node->NID) + " [label=\"" + origBlocks + "\"];\n";
        }
    }
    // now build out the nodes in the graph
    for (const auto &edge : graph.edges() )
    {
        if (auto call = dynamic_pointer_cast<CallEdge>(edge))
        {
            dotString += "\t" + to_string(call->getSrc()->NID) + " -> " + to_string(call->getSnk()->NID) + " [style=dashed, color=red, label=\""+to_string(call->getFreq())+","+to_string_float(call->getWeight()) + "\"];\n";
        }
        else if (auto ret = dynamic_pointer_cast<ReturnEdge>(edge))
        {
            dotString += "\t" + to_string(ret->getSrc()->NID) + " -> " + to_string(ret->getSnk()->NID) + " [style=dashed, color=blue, label=\""+to_string(ret->getFreq())+","+to_string_float(ret->getWeight()) + "\"];\n";
        }
        else if (auto cond = dynamic_pointer_cast<ConditionalEdge>(edge))
        {
            dotString += "\t" + to_string(cond->getSrc()->NID) + " -> " + to_string(cond->getSnk()->NID) + " [style=dotted, label=\""+to_string(cond->getFreq())+","+to_string_float(cond->getWeight()) + "\"];\n";
        }
        else if( auto ie = dynamic_pointer_cast<ImaginaryEdge>(edge) )
        {
            dotString += "\t" + to_string(ie->getSrc()->NID) + " -> " + to_string(ie->getSnk()->NID) + " [label=Imaginary];\n";
        }
        else if( auto ue = dynamic_pointer_cast<UnconditionalEdge>(edge) )
        {
            dotString += "\t" + to_string(ue->getSrc()->NID) + " -> " + to_string(ue->getSnk()->NID) + " [label=\""+to_string(ue->getFreq())+","+to_string_float(1.0f)+"\"];\n";
        }
        else
        {
            throw AtlasException("Could not determine edge type in graph print!");
        }
    }
    for( const auto& node : graph.nodes() )
    {
        if (auto VKN = dynamic_pointer_cast<MLCycle>(node))
        {
            deque<MLCycle *> Q;
            Q.push_back(VKN.get());
            while (!Q.empty())
            {
                for (const auto &c : Q.front()->getChildKernels())
                {
                    dotString += "\t" + to_string(c->NID) + " -> " + to_string(Q.front()->NID) + " [style=dashed];\n";
                    Q.push_back(c.get());
                }
                Q.pop_front();
            }
        }
    }
    dotString += "}";
    return dotString;
}

string Cyclebite::Graph::GenerateCoverageDot(const set<std::shared_ptr<ControlNode>, p_GNCompare> &coveredNodes, const set<std::shared_ptr<ControlNode>, p_GNCompare> &uncoveredNodes)
{
    string dotString = "digraph{\n";
    // label nodes based on their original blocks and color them based on whether they are covered or uncovered
    set<std::shared_ptr<ControlNode>, p_GNCompare> combined;
    set_union(coveredNodes.begin(), coveredNodes.end(), uncoveredNodes.begin(), uncoveredNodes.end(), std::inserter(combined, combined.begin()));
    for (const auto &node : combined)
    {
        string origBlocks = "";
        auto block = node->originalBlocks.rbegin();
        if (node->originalBlocks.empty())
        {
            continue;
        }
        origBlocks += to_string(*block);
        if (markovOrder > 1 && (node->originalBlocks.size() > 1))
        {
            origBlocks += "|";
            block++;
            origBlocks += to_string(*block);
            block++;
            for (; block != node->originalBlocks.rend(); block++)
            {
                origBlocks += "," + to_string(*block);
            }
        }
        if (coveredNodes.find(node) != coveredNodes.end())
        {
            dotString += "\t" + to_string(node->NID) + " [label=\"" + origBlocks + "\",style=filled,color=blue,fontcolor=white];\n";
        }
        else
        {
            dotString += "\t" + to_string(node->NID) + " [label=\"" + origBlocks + "\",style=filled,color=red,fontcolor=black];\n";
        }
    }
    // now build out the nodes in the graph
    for (const auto &node : combined)
    {
        for (const auto &n : node->getSuccessors())
        {
            dotString += "\t" + to_string(n->getSrc()->NID) + " -> " + to_string(n->getSnk()->NID) + " [label=" + to_string_float(n->getWeight()) + "];\n";
        }
        if (auto VKN = dynamic_pointer_cast<MLCycle>(node))
        {
            for (const auto &p : VKN->getParentKernels())
            {
                dotString += "\t" + to_string(node->NID) + " -> " + to_string(p->KID) + " [style=dashed];\n";
            }
        }
    }
    dotString += "}";
    return dotString;
}

void BuildSubgraph(std::shared_ptr<MLCycle> toBuild, const set<std::shared_ptr<MLCycle>, KCompare> &kernels, map<uint64_t, uint64_t> &BlockToNode, string &dotString, string &tabString, int &KToNode)
{
    // build out toBuild subgraph
    dotString += tabString + "subgraph cluster_" + to_string(KToNode) + "{\n";
    dotString += tabString + "\tlabel=\"Kernel " + to_string(KToNode++) + "\";\n";
    for (auto b : toBuild->blocks)
    {
        if (BlockToNode.find((uint64_t)b) == BlockToNode.end())
        {
            continue;
        }
        dotString += tabString + "\t" + to_string(BlockToNode[(uint64_t)b]) + ";\n";
    }
    // now recurse onto each of our children
    tabString += "\t";
    for (const auto &kern : toBuild->getChildKernels())
    {
        auto child = *kernels.find(kern);
        BuildSubgraph(child, kernels, BlockToNode, dotString, tabString, KToNode);
    }
    // now close our subgraph
    tabString.pop_back();
    dotString += tabString + "}\n";
}

string Cyclebite::Graph::GenerateTransformedSegmentedDot(const set<std::shared_ptr<ControlNode>, p_GNCompare> &nodes, const set<std::shared_ptr<MLCycle>, KCompare> &kernels, int markovOrder)
{
    // create a node to block mapping
    map<uint64_t, uint64_t> BlockToNode;
    for (const auto &node : nodes)
    {
        BlockToNode[(uint64_t)(*node->blocks.begin())] = node->NID;
    }
    string dotString = "digraph{\n";
    int j = 0;
    // here we build the kernel group clusters
    // kernel hierarchies have to be defined within each other in the dotfile structure
    // thus we have to go through each kernels hierarchy starting from the parent kernels
    // this set keeps track of all parent-less kernels that have been evaluated
    set<std::shared_ptr<MLCycle>, KCompare> doneKernels;
    for (const auto &kernel : kernels)
    {
        if (kernel->getParentKernels().empty())
        {
            // we need to perform a depth-first search of the hieararchy of kernels under this root kernel
            string tabString = "\t";
            BuildSubgraph(kernel, kernels, BlockToNode, dotString, tabString, j);
        }
    }

    // label nodes based on their original blocks
    for (const auto &node : nodes)
    {
        string origBlocks = "";
        auto block = node->originalBlocks.rbegin();
        if (node->originalBlocks.empty())
        {
            continue;
        }
        origBlocks += to_string(*block);
        if (markovOrder > 1 && (node->originalBlocks.size() > 1))
        {
            origBlocks += "|";
            block++;
            origBlocks += to_string(*block);
            block++;
            for (; block != node->originalBlocks.rend(); block++)
            {
                origBlocks += "," + to_string(*block);
            }
        }
        dotString += "\t" + to_string(node->NID) + " [label=\"" + origBlocks + "\"];\n";
    }
    // now build out the nodes in the graph
    for (const auto &node : nodes)
    {
        for (const auto &n : node->getSuccessors())
        {
            dotString += "\t" + to_string(n->getSrc()->NID) + " -> " + to_string(n->getSnk()->NID) + " [label=" + to_string_float(n->getWeight()) + "];\n";
        }
    }
    dotString += "}";
    return dotString;
}

double Cyclebite::Graph::EntropyCalculation(const std::set<std::shared_ptr<ControlNode>, p_GNCompare> &nodes)
{
    /// Ben 5/17/22 This is wrong and needs to be refactored
    /// To calculate the stationary distribution correctly, we need to solve the following equation for x
    /// x = xP
    /// where
    /// x is a vector of stationary distributions for each node in the graph
    /// P is the normalized transition table of the graph (each column sums to 1)
    // first, calculate the stationary distribution for each existing node
    // stationary distribution is the probability that I am in a certain state at any given time (it's an asymptotic measure)
    vector<double> stationaryDistribution(nodes.size(), 0.0);
    for (unsigned int i = 0; i < nodes.size(); i++)
    {
        auto it = nodes.begin();
        advance(it, i);
        // we sum along the columns (the probabilities of going to the current node), so we use the edge weight coming from each predecessor to this node
        for (const auto &pred : (*it)->getPredecessors())
        {
            // retrieve the edge probability and accumulate it to this node
            stationaryDistribution[i] += (double)pred->getFreq();
        }
    }
    // normalize each stationaryDistribution entry by the total edge weights in the state transition matrix
    double totalEdgeWeights = 0.0;
    for (const auto &node : nodes)
    {
        for (const auto &nei : node->getSuccessors())
        {
            totalEdgeWeights += (double)nei->getFreq();
        }
    }
    for (auto &entry : stationaryDistribution)
    {
        entry /= totalEdgeWeights;
    }
    // second, calculate the average entropy of each node (the entropy rate)
    double entropyRate = 0.0;
    for (unsigned int i = 0; i < stationaryDistribution.size(); i++)
    {
        auto it = nodes.begin();
        advance(it, i);
        for (const auto &nei : (*it)->getSuccessors())
        {
            entropyRate -= stationaryDistribution[i] * nei->getWeight() * log2(nei->getWeight());
        }
    }
    return entropyRate;
}

double Cyclebite::Graph::TotalEntropy(const std::set<std::shared_ptr<ControlNode>, p_GNCompare> &nodes)
{
    double accumulatedEntropy = 0.0;
    for (const auto &node : nodes)
    {
        for (const auto &nei : node->getSuccessors())
        {
            accumulatedEntropy -= nei->getWeight() * log2(nei->getWeight());
        }
    }
    return accumulatedEntropy;
}

using json = nlohmann::json;

/// @brief Used to find the underlying edge that occurs before or after the 
const shared_ptr<UnconditionalEdge> FindUnderlyingEdge(const shared_ptr<GraphNode>& node, bool entrance)
{
    deque<shared_ptr<GraphNode>> Q;
    set<shared_ptr<ControlNode>, p_GNCompare> underlying;
    Q.push_front(node);
    while( !Q.empty() )
    {
        if( auto vn = dynamic_pointer_cast<VirtualNode>(Q.front()) )
        {
            for( const auto& n : vn->getSubgraph() )
            {
                Q.push_back(n);
            }
        }
        else if( auto cn = dynamic_pointer_cast<ControlNode>(Q.front()) )
        {
            underlying.insert(cn);
        }
    }
    set<shared_ptr<UnconditionalEdge>, GECompare> underlying_e;
    for( const auto& n : underlying )
    {
        for( const auto& pred : n->getPredecessors() )
        {
            if( underlying.find(pred->getSrc()) != underlying.end() )
            {
                underlying_e.insert(pred);
            }
        }
        for( const auto& succ : n->getSuccessors() )
        {
            if( underlying.find(succ->getSnk()) != underlying.end() )
            {
                underlying_e.insert(succ);
            }
        }
    }
    if( entrance )
    {
        // we want the edge that comes out of the first node
        shared_ptr<ControlNode> first = nullptr;
        for( const auto& node : underlying ) 
        {
            bool outside = true;
            for( const auto& pred : node->getPredecessors() )
            {
                if( underlying.find(pred->getSrc()) != underlying.end() )
                {
                    outside = false;
                    break;
                }
            }
            if( outside )
            {
                first = node;
                break;
            }
        }
        if( first )
        {
            if( first->getSuccessors().size() != 1 )
            {
                throw AtlasException("Cannot handle the case where an underlying entrance has more than one successor!");
            }
            else
            {
                return *first->getSuccessors().begin();
            }
        }
        else
        {
            throw AtlasException("No beginning node could be found for subgraph!");
        }
    }
    else
    {
        // we want the edge that precedes the last node in the graph
        shared_ptr<ControlNode> last = nullptr;
        for( const auto& node : underlying )
        {
            bool outside = true;
            for( const auto& succ : node->getSuccessors() )
            {
                if( underlying.find( succ->getSnk()) != underlying.end() )
                {
                    outside = false;
                    break;
                }
            }
            if( outside )
            {
                last = node;
                break;
            }
        }
        if( last )
        {
            if( last->getPredecessors().size() != 1 )
            {
                throw AtlasException("Cannot handle the case where an underlying exit has more than one predecessor!");
            }
            else
            {
                return *last->getPredecessors().begin();
            }
        }
        else
        {
            throw AtlasException("No ending node could be found for subgraph!");
        }
    }
}

set<pair<int64_t, int64_t>> Cyclebite::Graph::findOriginalBlockIDs(const shared_ptr<UnconditionalEdge>& edge)
{
    struct EdgeSort
    {
        // the following operator implements a "less than" operator ie lhs < rhs
        bool operator()(const shared_ptr<UnconditionalEdge>& lhs, const shared_ptr<UnconditionalEdge>& rhs) const
        {
            if( lhs->getSnk() == rhs->getSrc() )
            {
                // lhs is "less than" rhs because lhs comes before rhs in the graph
                return true;
            }
            else if( lhs->getSrc() == rhs->getSnk() )
            {
                // lhs is "greater than" rhs because lhs comes after rhs in the graph
                return false;
            }
            else if( lhs->getSrc() == rhs->getSrc() )
            {
                // on a match with one node we compare the other side
                // thus if two edges have matching sources but different sinks, they are put in a random order
                // and, if two edges have the same src and snk, they will match in either case
                return lhs->getSnk()->NID < rhs->getSnk()->NID;
            }
            else if( lhs->getSnk() == rhs->getSnk() )
            {
                return lhs->getSrc()->NID < rhs->getSrc()->NID;
            }
            else
            {
                // when there is no overlap at all between two edges, we simply sort by their pointer
                return lhs.get() < rhs.get();
            }
        }
    };
    set<pair<int64_t, int64_t>> eEdges;
    deque<shared_ptr<UnconditionalEdge>> Q;
    Q.push_front(edge);
    try
    {
        while (!Q.empty())
        {
            if (auto ve = dynamic_pointer_cast<VirtualEdge>(Q.front()))
            {
                if( ve->getEdges().size() == 1 )
                {
                    Q.push_back(*ve->getEdges().begin());
                }
                else if( ve->getEdges().empty() )
                {
                    throw AtlasException("Virtual edge has no underlying edges!");
                }
                else
                {
                    for( const auto& e : ve->getEdges() )
                    {
                        Q.push_back(e);
                    }
                }
            }
            else if( auto ie = dynamic_pointer_cast<ImaginaryEdge>(Q.front()) )
            {
                // we have hit the start or end of the program
                // if it is the start of the program we want to return the edge that comes after us
                // if we are at the end we want to return the edge that comes right before
                if( ie->isEntrance() )
                {
                    // finding an imaginary edge reveals that we actually started on the wrong edge
                    // so we are going to basically start the algorithm over by pushing the edge that comes immediately after the snk node
                    Q.push_back(FindUnderlyingEdge(ie->getSnk(), true));
                }
                else
                {
                    Q.push_back(FindUnderlyingEdge(ie->getSrc(), false));
                }
            }
            else
            {
                // we have found the original edge, its src node should have the originalBlock it was constructed for
                if (!Q.front()->getWeightedSrc()->originalBlocks.empty() && !Q.front()->getWeightedSnk()->originalBlocks.empty())
                {
                    auto srcBlock = Q.front()->getWeightedSrc()->originalBlocks.back();
                    auto snkBlock = Q.front()->getWeightedSnk()->originalBlocks.back();
                    eEdges.insert( pair(srcBlock, snkBlock) );
                }
                else
                {
                    throw AtlasException("Rock bottom nodes did not contain  original blocks!");
                }
            }
            Q.pop_front();
        }
        if (eEdges.empty())
        {
            throw AtlasException("Could not map graph edge to src,snk pair!");
        }
    }
    catch( AtlasException& e )
    {
        spdlog::critical(e.what());
        exit(EXIT_FAILURE);
    }
    return eEdges;
}

set<int64_t> findOriginalBlockIDs(const shared_ptr<ControlNode>& ent)
{
    set<int64_t> originalBlocks;
    deque<shared_ptr<ControlNode>> Q;
    set<shared_ptr<ControlNode>> covered;
    Q.push_front(ent);
    covered.insert(ent);
    try
    {
        while( !Q.empty() )
        {
            if( auto vn = dynamic_pointer_cast<VirtualNode>(Q.front()) )
            {
                for( const auto& sn : vn->getSubgraph() )
                {
                    if( covered.find(sn) == covered.end() )
                    {
                        Q.push_back(sn);
                        covered.insert(sn);
                    }
                }
            }
            else
            {
                if (!Q.front()->originalBlocks.empty())
                {
                    originalBlocks.insert(Q.front()->originalBlocks.back());
                }
                else
                {
                    throw AtlasException("Rock bottom node did not contain original blocks!");
                }
            }
            Q.pop_front();
        }
    }
    catch( AtlasException& e )
    {
        spdlog::critical(e.what());
        return originalBlocks;
    }
    return originalBlocks;
}

void Cyclebite::Graph::WriteKernelFile(const ControlGraph &graph, const set<std::shared_ptr<MLCycle>, KCompare> &kernels, const map<int64_t, llvm::BasicBlock *> &IDToBlock, const map<int64_t, std::vector<int64_t>> &blockCallers, const EntropyInfo &info, const string &OutputFileName, bool hotCode)
{
    // write kernel file
    json outputJson;
    // valid blocks and block callers sections provide tik with necessary info about the CFG
    outputJson["ValidBlocks"] = std::vector<int64_t>();
    for (const auto &id : IDToBlock)
    {
        outputJson["ValidBlocks"].push_back(id.first);
    }
    for (const auto &bid : blockCallers)
    {
        outputJson["BlockCallers"][to_string(bid.first)] = bid.second;
    }
    // Entropy information
    outputJson["Entropy"] = map<string, map<string, uint64_t>>();
    outputJson["Entropy"]["Start"]["Entropy Rate"] = info.start_entropy_rate;
    outputJson["Entropy"]["Start"]["Total Entropy"] = info.start_total_entropy;
    outputJson["Entropy"]["Start"]["Nodes"] = info.start_node_count;
    outputJson["Entropy"]["Start"]["Edges"] = info.start_edge_count;
    outputJson["Entropy"]["End"]["Entropy Rate"] = info.end_entropy_rate;
    outputJson["Entropy"]["End"]["Total Entropy"] = info.end_total_entropy;
    outputJson["Entropy"]["End"]["Nodes"] = info.end_node_count;
    outputJson["Entropy"]["End"]["Edges"] = info.end_edge_count;

    // sequential ID for each kernel and a map from KID to sequential ID
    uint32_t id = 0;
    map<uint32_t, uint32_t> SIDMap;
    // average nodes per kernel
    float totalNodes = 0.0;
    // average blocks per kernel
    float totalBlocks = 0.0;
    for (const auto &kernel : kernels)
    {
        totalNodes += (float)kernel->getSubgraph().size();
        totalBlocks += (float)kernel->blocks.size();
        for (const auto &n : kernel->getSubgraph())
        {
            outputJson["Kernels"][to_string(id)]["Nodes"].push_back(n->NID);
        }
        for (const auto &k : kernel->blocks)
        {
            outputJson["Kernels"][to_string(id)]["Blocks"].push_back(k);
        }
        outputJson["Kernels"][to_string(id)]["Labels"] = std::vector<string>();
        outputJson["Kernels"][to_string(id)]["Labels"].push_back(kernel->Label);
        // entrances and exits
        for (const auto &e : kernel->getEntrances())
        {
            // we need to figure out which blocks are on the border of this entrance edge
            auto entrances = findOriginalBlockIDs(e);
            for( const auto& ent : entrances )
            {
                outputJson["Kernels"][to_string(id)]["Entrances"][to_string(ent.first)].push_back(to_string(ent.second));
            }
        }
        for (const auto &e : kernel->getExits())
        {
            // we need to figure out which blocks are on the border of this entrance edge
            auto exits = findOriginalBlockIDs(e);
            for( const auto& ex : exits )
            {
                outputJson["Kernels"][to_string(id)]["Exits"][to_string(ex.first)].push_back(to_string(ex.second));
            }
        }
        SIDMap[kernel->KID] = id;
        id++;
    }
    // now assign hierarchy to each kernel
    for (const auto &kern : kernels)
    {
        outputJson["Kernels"][to_string(SIDMap[kern->KID])]["Children"] = vector<uint32_t>();
        outputJson["Kernels"][to_string(SIDMap[kern->KID])]["Parents"] = vector<uint32_t>();
    }
    // fill in parent category for children while we're filling in the children
    for (const auto &kern : kernels)
    {
        for (const auto &child : kern->getChildKernels())
        {
            outputJson["Kernels"][to_string(SIDMap[kern->KID])]["Children"].push_back(SIDMap[child->KID]);
            outputJson["Kernels"][to_string(SIDMap[child->KID])]["Parents"].push_back(SIDMap[kern->KID]);
        }
    }
    // introspect non-kernel code, and make a set of non-kernel blocks
    // for whatever is left in the graph, code that does not belong to a kernel is non-kernel code
    set<int64_t> nonKernelBlocks;
    for (const auto &node : graph.nodes())
    {
        if (auto ml = dynamic_pointer_cast<MLCycle>(node))
        {
            // do nothing
        }
        else if (auto vn = dynamic_pointer_cast<VirtualNode>(node))
        {
            // get the blocks of this virtual node and add them to the nonkernel block set
            deque<shared_ptr<VirtualNode>> Q;
            Q.push_front(vn);
            shared_ptr<ControlNode> re = static_pointer_cast<ControlNode>(node);
            while (!Q.empty())
            {
                for (const auto &sub : Q.front()->getSubgraph())
                {
                    if (auto subml = dynamic_pointer_cast<MLCycle>(sub))
                    {
                        // do nothing
                    }
                    else if (auto subve = dynamic_pointer_cast<VirtualNode>(sub))
                    {
                        Q.push_back(subve);
                    }
                    else if (auto cn = dynamic_pointer_cast<ControlNode>(sub))
                    {
                        nonKernelBlocks.insert(cn->blocks.begin(), cn->blocks.end());
                    }
                }
                Q.pop_front();
            }
        }
        else if (auto cn = dynamic_pointer_cast<ControlNode>(node))
        {
            nonKernelBlocks.insert(cn->blocks.begin(), cn->blocks.end());
        }
    }
    outputJson["NonKernelBlocks"] = nonKernelBlocks;

    if( !hotCode )
    {
        // build the dominator tree for kernels
        // a dominator tree is a tree where edges point from dominator to dominatee
        // each key in the map points to a set of its dominator kernels
        // this requires that we have a graph that has all parent-most kernels uncovered
        auto unrolledGraph = reverseTransform_MLCycle(graph);
        map<shared_ptr<MLCycle>, set<shared_ptr<MLCycle>, p_GNCompare>> dominators;
        for( const auto& kern : kernels )
        {
            dominators[kern] = set<shared_ptr<MLCycle>, p_GNCompare>();
        }

        deque<shared_ptr<ControlNode>> Q;
        set<shared_ptr<ControlNode>, p_GNCompare> covered;
        set<shared_ptr<MLCycle>, p_GNCompare> seenKernels;
        Q.push_front(unrolledGraph.getFirstNode());
        covered.insert(unrolledGraph.getFirstNode());
        while( !Q.empty() )
        {
            if( auto mlc = dynamic_pointer_cast<MLCycle>(Q.front()) )
            {
                dominators[mlc].insert(seenKernels.begin(), seenKernels.end());
                seenKernels.insert(mlc);
                // all children of this mlc are dominated by this mlc
                deque<shared_ptr<MLCycle>> hierarchy;
                for( const auto& c : mlc->getChildKernels() )
                {
                    hierarchy.push_front(c);
                }
                while( !hierarchy.empty() )
                {
                    dominators[hierarchy.front()].insert(mlc);
                    for( const auto& c : hierarchy.front()->getChildKernels() )
                    {
                        hierarchy.push_back(c);
                    }
                    hierarchy.pop_front();
                }
            }
            for( const auto& succ : Q.front()->getSuccessors() )
            {
                if( covered.find(succ->getWeightedSnk()) == covered.end() )
                {
                    Q.push_back(succ->getWeightedSnk());
                    covered.insert(succ->getWeightedSnk());
                }
            }
            Q.pop_front();
        }

        for( const auto& kern : dominators )
        {
            set<uint32_t> doms;
            for( const auto& dom : kern.second )
            {
                doms.insert(SIDMap.at(dom->KID));
            }
            outputJson["Kernels"][to_string(SIDMap.at(kern.first->KID))]["Dominators"] = doms;
        }
        /*
        // for each kernel, find its dominating kernels
        // a dominating kernel is a kernel that must execute before the target kernel
        for( const auto& kern : kernels )
        {
            for( const auto& domKern : kernels )
            {
                if( kern == domKern )
                {
                    continue;
                }
                // look at the exit block of this kernel and figure out if it dominates the entrance block of this kernel
                for( const auto& ex : domKern->getExits() )
                {
                    auto exitBlock = NodeToBlock(ex->getWeightedSnk(), IDToBlock);
                    for( const auto& ent : kern->getEntrances() )
                    {
                        auto entBlock = NodeToBlock(ent->getWeightedSnk(), IDToBlock);
                        // now we have to see if the exitblock of the dominator kernel dominates the entrance block of the current kernel in question
                        // first find their closest common ancestor in the callgraph of the program
                        // second, find the basic blocks in which their function calls exist
                        // third, run the dominator thing on them
                    }
                }
            }
            // this set contains all blocks that are not part of kernels and predicate this kernel
            set<int64_t> predicateBlocks;
            // now walk back from this parent kernel until we find a block that is not a non-kernel block
            deque<shared_ptr<ControlNode>> Q;
            set<shared_ptr<ControlNode>, p_GNCompare> covered;
            covered.insert(kern->getSubgraph().begin(), kern->getSubgraph().end());
            for( const auto& ent : kern->getEntrances() )
            {
                if( dynamic_pointer_cast<MLCycle>(ent->getWeightedSrc()) == nullptr )
                {
                    if( covered.find(ent->getWeightedSrc()) == covered.end() )
                    {
                        Q.push_back(ent->getWeightedSrc());
                        covered.insert(ent->getWeightedSrc());
                    }
                }
                while( !Q.empty() )
                {
                    auto blocks = findOriginalBlockIDs(Q.front());
                    predicateBlocks.insert(blocks.begin(), blocks.end());
                    for( const auto& pred : Q.front()->getPredecessors() )
                    {
                        if( dynamic_pointer_cast<MLCycle>(pred->getSrc()) == nullptr )
                        {
                            if( covered.find(pred->getWeightedSrc()) == covered.end() )
                            {
                                Q.push_back(pred->getWeightedSrc());
                                covered.insert(pred->getWeightedSrc());
                            }
                        }
                    }
                    for( const auto& succ : Q.front()->getSuccessors() )
                    {
                        if( dynamic_pointer_cast<MLCycle>(succ->getSnk()) == nullptr )
                        {
                            if( covered.find(succ->getWeightedSnk()) == covered.end() )
                            {
                                Q.push_back(succ->getWeightedSnk());
                                covered.insert(succ->getWeightedSnk());
                            }
                        }
                    }
                    Q.pop_front();
                }
            }
            outputJson["Kernels"][to_string(SIDMap.at(kern->KID))]["PredicateBlocks"] = predicateBlocks;
            // now find successorBlocks
            // a successor block is a block that cannot reach back to this kernel ie when it has executed we know this kernel cannot possibly be live
            set<int64_t> successorBlocks;
            Q.clear();
            covered.clear();
            covered.insert(kern->getSubgraph().begin(), kern->getSubgraph().end());
            for( const auto& ex : kern->getExits() )
            {
                if( dynamic_pointer_cast<MLCycle>(ex->getWeightedSnk()) == nullptr )
                {
                    if( covered.find(ex->getWeightedSnk()) == covered.end() )
                    {
                        Q.push_back(ex->getWeightedSnk());
                        covered.insert(ex->getWeightedSnk());
                    }
                }
                while( !Q.empty() )
                {
                    // do dijkstras between the current node and each entrance
                    bool cycleFound = false;
                    for( auto ent : kern->getEntrances() )
                    {
                        auto cycle = Cyclebite::Graph::Dijkstras(graph, Q.front()->NID, ent->getSnk()->NID);
                        if( !cycle.empty() )
                        {
                            cycleFound = true;
                            break;
                        }
                    }
                    if( !cycleFound )
                    {
                        auto blocks = findOriginalBlockIDs(Q.front());
                        successorBlocks.insert(blocks.begin(), blocks.end());
                    }
                    for( const auto& pred : Q.front()->getPredecessors() )
                    {
                        if( dynamic_pointer_cast<MLCycle>(pred->getSrc()) == nullptr )
                        {
                            if( covered.find(pred->getWeightedSrc()) == covered.end() )
                            {
                                Q.push_back(pred->getWeightedSrc());
                                covered.insert(pred->getWeightedSrc());
                            }
                        }
                    }
                    for( const auto& succ : Q.front()->getSuccessors() )
                    {
                        if( dynamic_pointer_cast<MLCycle>(succ->getSnk()) == nullptr )
                        {
                            if( covered.find(succ->getWeightedSnk()) == covered.end() )
                            {
                                Q.push_back(succ->getWeightedSnk());
                                covered.insert(succ->getWeightedSnk());
                            }
                        }
                    }
                    Q.pop_front();
                }
            }
            outputJson["Kernels"][to_string(SIDMap.at(kern->KID))]["SuccessorBlocks"] = successorBlocks;
        }*/
    }

    if (!kernels.empty())
    {
        outputJson["Average Kernel Size (Nodes)"] = float(totalNodes / (float)kernels.size());
        outputJson["Average Kernel Size (Blocks)"] = float(totalBlocks / (float)kernels.size());
    }
    else
    {
        outputJson["Average Kernel Size (Nodes)"] = 0.0;
        outputJson["Average Kernel Size (Blocks)"] = 0.0;
    }

    // performance intrinsics
    map<string, set<int64_t>> kernelBlockSets;
    for (const auto &kernel : outputJson["Kernels"].items())
    {
        if (outputJson["Kernels"].find(kernel.key()) != outputJson["Kernels"].end())
        {
            if (outputJson["Kernels"][kernel.key()].find("Blocks") != outputJson["Kernels"][kernel.key()].end())
            {
                auto blockSet = outputJson["Kernels"][kernel.key()]["Blocks"].get<set<int64_t>>();
                kernelBlockSets[kernel.key()] = blockSet;
            }
        }
    }

    ofstream oStream(OutputFileName);
    oStream << setw(4) << outputJson;
    oStream.close();
}

/// TODO: add function calls to this graph, right now it completely skips them
ControlGraph Cyclebite::Graph::GenerateStaticCFG(llvm::Module *M)
{
    ControlGraph staticGraph;
    for (auto f = M->begin(); f != M->end(); f++)
    {
        for (auto b = f->begin(); b != f->end(); b++)
        {
            std::shared_ptr<ControlNode> newNode = nullptr;
            if (staticGraph.find_node((uint64_t)GetBlockID(cast<BasicBlock>(b))))
            {
                newNode = staticGraph.getNode((uint64_t)GetBlockID(cast<BasicBlock>(b)));
            }
            else
            {
                newNode = make_shared<ControlNode>();
                newNode->originalBlocks.push_back((uint32_t)newNode->NID);
            }
            for (uint32_t i = 0; i < b->getTerminator()->getNumSuccessors(); i++)
            {
                std::shared_ptr<ControlNode> succ = nullptr;
                if (staticGraph.find_node((uint64_t)GetBlockID(b->getTerminator()->getSuccessor(i))))
                {
                    succ = staticGraph.getNode((uint64_t)GetBlockID(b->getTerminator()->getSuccessor(i)));
                }
                else
                {
                    succ = make_shared<ControlNode>();
                    succ->originalBlocks.push_back((uint32_t)succ->NID);
                    staticGraph.addNode(succ);
                }
                if (b->getTerminator()->getNumSuccessors() > 1)
                {
                    auto succEdge = make_shared<UnconditionalEdge>(0, newNode, succ);
                    newNode->addSuccessor(succEdge);
                    succ->addPredecessor(succEdge);
                    staticGraph.addEdge(succEdge);
                }
                else
                {
                    auto succEdge = make_shared<ConditionalEdge>(0, newNode, succ);
                    newNode->addSuccessor(succEdge);
                    succ->addPredecessor(succEdge);
                    staticGraph.addEdge(succEdge);
                }
            }
            for (auto i = pred_begin(cast<BasicBlock>(b)); i != pred_end(cast<BasicBlock>(b)); i++)
            {
                std::shared_ptr<ControlNode> pred = nullptr;
                if (staticGraph.find_node((uint64_t)GetBlockID(cast<BasicBlock>(*i))))
                {
                    pred = staticGraph.getNode((uint64_t)GetBlockID(cast<BasicBlock>(*i)));
                }
                else
                {
                    pred = make_shared<ControlNode>();
                    pred->originalBlocks.push_back((uint32_t)pred->NID);
                    staticGraph.addNode(pred);
                }
                if ((*i)->getTerminator()->getNumSuccessors() > 1)
                {
                    auto predEdge = make_shared<UnconditionalEdge>(0, pred, newNode);
                    pred->addSuccessor(predEdge);
                    newNode->addPredecessor(predEdge);
                    staticGraph.addEdge(predEdge);
                }
                else
                {
                    auto predEdge = make_shared<ConditionalEdge>(0, pred, newNode);
                    pred->addSuccessor(predEdge);
                    newNode->addPredecessor(predEdge);
                    staticGraph.addEdge(predEdge);
                }
            }
        }
    }
    return staticGraph;
}

void Cyclebite::Graph::GenerateDynamicCoverage(const std::set<std::shared_ptr<ControlNode>, p_GNCompare> &dynamicNodes, const std::set<std::shared_ptr<ControlNode>, p_GNCompare> &staticNodes)
{
    // we need a static to dynamic node mapping
    map<std::shared_ptr<ControlNode>, set<std::shared_ptr<ControlNode>, p_GNCompare>> StaticToDynamic;
    // each static node has an NID that matches its blockID
    // so these loops find dynamic blocks who have a static node ID (BBID) in their original blocks
    for (const auto &stat : staticNodes)
    {
        for (const auto &dyn : dynamicNodes)
        {
            for (const auto &block : dyn->originalBlocks)
            {
                if (block == stat->NID)
                {
                    StaticToDynamic[stat].insert(dyn);
                }
            }
        }
    }
    // for now we just want to show how much of the static graph is covered at runtime
    // therefore we want to color each covered static node a certain way and all other static nodes another way
    set<std::shared_ptr<ControlNode>, p_GNCompare> covered;
    set<std::shared_ptr<ControlNode>, p_GNCompare> uncovered;
    for (const auto &node : StaticToDynamic)
    {
        if (!node.second.empty())
        {
            covered.insert(node.first);
        }
        else
        {
            uncovered.insert(node.first);
        }
    }
    auto dot = GenerateCoverageDot(covered, uncovered);
    ofstream dotStream("DynamicCoverage.dot");
    dotStream << dot << "\n";
    dotStream.close();
}

// Data graph operations
string Cyclebite::Graph::GenerateDataDot(const set<shared_ptr<DataValue>, p_GNCompare> &nodes)
{
    string dotString = "digraph{\n";
    // label nodes based on their operations
    for (const auto &node : nodes)
    {
        if( const auto& n = dynamic_pointer_cast<Inst>(node) )
        {
            dotString += "\t" + to_string(n->NID) + " [label=\"" + OperationToString[n->op] + "\"];\n";
        }
        else
        {
            dotString += "\t" + to_string(node->NID) + ";\n";
        }
    }
    // now build out the nodes in the graph
    for (const auto &node : nodes)
    {
        for (const auto &n : node->getSuccessors())
        {
            dotString += "\t" + to_string(n->getSrc()->NID) + " -> " + to_string(n->getSnk()->NID) + ";\n";
        }
    }
    dotString += "}";

    return dotString;
}

string Cyclebite::Graph::GenerateBBSubgraphDot(const set<std::shared_ptr<ControlBlock>, p_GNCompare> &BBs)
{
    string dotString = "digraph{\n\tcompound=true;\n";
    // basic block clusters
    map<uint64_t, uint64_t> BBToSubgraph;
    uint64_t j = 0;
    for (const auto &BB : BBs)
    {
        BBToSubgraph[BB->NID] = j;
        dotString += "\tsubgraph cluster_" + to_string(j) + "{\n";
        dotString += "\t\tlabel=\"Basic Block " + to_string(*BB->originalBlocks.begin()) + "\";\n";
        for (auto i : BB->instructions)
        {
            dotString += "\t\t" + to_string(i->NID) + ";\n";
        }
        dotString += "\t}\n";
        j++;
    }
    // label basic block clusters
    for (const auto &BB : BBs)
    {
        // label nodes based on their operations
        for (const auto &node : BB->instructions)
        {
            if (node->isState())
            {
                dotString += "\t" + to_string(node->NID) + " [color=red,label=\"" + OperationToString[node->op] + "\"];\n";
            }
            else if (node->isMemory())
            {
                dotString += "\t" + to_string(node->NID) + " [color=blue,label=\"" + OperationToString[node->op] + "\"];\n";
            }
            else if (node->isFunction())
            {
                dotString += "\t" + to_string(node->NID) + " [color=green,label=\"" + OperationToString[node->op] + "\"];\n";
            }
            else
            {
                dotString += "\t" + to_string(node->NID) + " [label=\"" + OperationToString[node->op] + "\"];\n";
            }
        }
        // now build out the nodes in the graph
        for (const auto &node : BB->instructions)
        {
            for (const auto &n : node->getSuccessors())
            {
                dotString += "\t" + to_string(n->getSrc()->NID) + " -> " + to_string(n->getSnk()->NID) + ";\n";
            }
            // build edges from the terminator of this basic block to the successors in the control flow graph
            if (node->isTerminator())
            {
                for (const auto &succ : node->parent->getSuccessors())
                {
                    if (BBs.find(succ->getSnk()->NID) != BBs.end())
                    {
                        dotString += "\t" + to_string(node->NID) + " -> " + to_string((*((*BBs.find(succ->getSnk()->NID))->instructions.begin()))->NID) + " [style=dashed,lhead=cluster_" + to_string(BBToSubgraph[(*BBs.find(succ->getSnk()->NID))->NID]) + ",label=" + to_string_float(succ->getWeight()) + "];\n";
                    }
                    // else this is a block outside the kernel... a kernel exit
                }
            }
            // draw lines from call instructions to their function bodies (if possible) then from their return instructions back to the caller
            else if( auto call = dynamic_pointer_cast<CallNode>(node) )
            {
                for( const auto& dest : call->getDestinations() )
                {
                    if( BBs.find(dest->NID) != BBs.end() )
                    {
                        dotString += "\t" + to_string(call->NID) + " -> " + to_string((*((*BBs.find(dest->NID))->instructions.begin()))->NID) + " [style=dotted,lhead=cluster_" + to_string(BBToSubgraph[(*BBs.find(dest->NID))->NID]) + "];\n";
                    }
                }
            }
        }
    }

    dotString += "}";
    return dotString;
}

string Cyclebite::Graph::GenerateHighlightedSubgraph(const Graph &graph, const Graph &subgraph)
{
    bool abridged = graph.edge_count() > MAX_EDGE_UNABRIDGED;
    string dotString = "digraph {\n";
    for (const auto &node : graph.nodes())
    {
        if (subgraph.find(node))
        {
            dotString += "\t" + to_string(node->NID) + " [color=blue];\n";
        }
    }
    for (const auto &edge : graph.edges())
    {
        if (subgraph.find(edge))
        {
            if (auto ce = dynamic_pointer_cast<CallEdge>(edge))
            {
                dotString += "\t" + to_string(ce->getSrc()->NID) + " -> " + to_string(ce->getSnk()->NID) + " [label=" + to_string_float(ce->getWeight()) + ",style=dashed,color=blue];\n";
            }
            else if( auto ue = dynamic_pointer_cast<UnconditionalEdge>(edge) )
            {
                dotString += "\t" + to_string(ue->getSrc()->NID) + " -> " + to_string(ue->getSnk()->NID) + " [label=" + to_string_float(ue->getWeight()) + ",color=blue];\n";
            }
            else if( auto i = dynamic_pointer_cast<ImaginaryEdge>(edge) )
            {
                dotString += "\t" + to_string(ue->getSrc()->NID) + " -> " + to_string(ue->getSnk()->NID) + " [label=Imaginary,color=blue];\n";
            }
            else
            {
                throw AtlasException("Could not determine type of edge in graph print!");
            }
        }
        else if( abridged )
        {
            if (subgraph.find(edge->getSrc()) || subgraph.find(edge->getSnk()))
            {
                if (auto ce = dynamic_pointer_cast<CallEdge>(edge))
                {
                    dotString += "\t" + to_string(ce->getSrc()->NID) + " -> " + to_string(ce->getSnk()->NID) + " [label=" + to_string_float(ce->getWeight()) + ",style=dashed];\n";
                }
                else if( auto ue = dynamic_pointer_cast<UnconditionalEdge>(edge) )
                {
                    dotString += "\t" + to_string(ue->getSrc()->NID) + " -> " + to_string(ue->getSnk()->NID) + " [label=" + to_string_float(ue->getWeight()) + "];\n";
                }
                else if( auto i = dynamic_pointer_cast<ImaginaryEdge>(edge) )
                {
                    dotString += "\t" + to_string(i->getSrc()->NID) + " -> " + to_string(i->getSnk()->NID) + " [label=Imaginary];\n";
                }
                else
                {
                    throw AtlasException("Could not determine type of edge in graph print!");
                }
            }
        }
        else
        {
            if (auto ce = dynamic_pointer_cast<CallEdge>(edge))
            {
                dotString += "\t" + to_string(ce->getSrc()->NID) + " -> " + to_string(ce->getSnk()->NID) + " [label=" + to_string_float(ce->getWeight()) + ",style=dashed];\n";
            }
            else if( auto ue = dynamic_pointer_cast<UnconditionalEdge>(edge) )
            {
                dotString += "\t" + to_string(ue->getSrc()->NID) + " -> " + to_string(ue->getSnk()->NID) + " [label=" + to_string_float(ue->getWeight()) + "];\n";
            }
            else if( auto i = dynamic_pointer_cast<ImaginaryEdge>(edge) )
            {
                dotString += "\t" + to_string(i->getSrc()->NID) + " -> " + to_string(i->getSnk()->NID) + " [label=Imaginary];\n";
            }
            else
            {
                throw AtlasException("Could not determine type of edge in graph print!");
            }
        }
    }
    dotString += "}";
    return dotString;
}

string Cyclebite::Graph::GenerateCallGraph(const llvm::CallGraph &CG)
{
    // assigns a unique idendifier to each node
    map<llvm::CallGraphNode *, uint32_t> IDs;
    uint32_t NID = 0;
    for (auto node = CG.begin(); node != CG.end(); node++)
    {
        if (IDs.find(node->second.get()) == IDs.end())
        {
            IDs[node->second.get()] = NID++;
        }
        for (auto child = node->second->begin(); child != node->second->end(); child++)
        {
            if (IDs.find(child->second) == IDs.end())
            {
                IDs[child->second] = NID++;
            }
        }
    }
    if (IDs.find(CG.getCallsExternalNode()) == IDs.end())
    {
        IDs[CG.getCallsExternalNode()] = NID++;
    }
    string dotString = "digraph {\n";
    // label function nodes
    for (auto node = CG.begin(); node != CG.end(); node++)
    {
        if (node->first)
        {
            dotString += "\t" + to_string(IDs.at(node->second.get())) + " [label=\"" + string(node->first->getName()) + "\"];\n";
        }
        else
        {
            dotString += "\t" + to_string(IDs.at(node->second.get())) + " [label=\"NullFunction\"];\n";
        }
    }
    // the CallsExternalNode is not included in the node set
    dotString += "\t" + to_string(IDs.at(CG.getCallsExternalNode())) + " [label=\"NullOrExternalFunction\"];\n";
    // draw parent->child edges
    for (auto node = CG.begin(); node != CG.end(); node++)
    {
        for (auto child = node->second->begin(); child != node->second->end(); child++)
        {
            dotString += "\t" + to_string(IDs.at(node->second.get())) + " -> " + to_string(IDs.at(child->second)) + ";\n";
        }
    }
    dotString += "}";
    return dotString;
}

string Cyclebite::Graph::GenerateCallGraph(const Cyclebite::Graph::CallGraph &CG)
{
    string dotString = "digraph {\n";
    // label function nodes
    for (auto node : CG.nodes())
    {
        dotString += "\t" + to_string(node->NID) + " [label=\"" + string(static_pointer_cast<CallGraphNode>(node)->getFunction()->getName()) + "\"];\n";
    }
    // draw parent->child edges
    for (auto edge : CG.edges())
    {
        dotString += "\t" + to_string(edge->getSrc()->NID) + " -> " + to_string(edge->getSnk()->NID) + ";\n";
    }
    dotString += "}";
    return dotString;
}

string Cyclebite::Graph::GenerateFunctionSubgraph(const Graph &funcGraph, const shared_ptr<CallEdge> &entrance)
{
    string dotString = "digraph {\n";
    dotString += "\t" + to_string(entrance->getSnk()->NID) + " [label=ENTRANCE];\n";
    for (const auto &ex : entrance->rets.dynamicRets)
    {
        dotString += "\t" + to_string(ex->getSrc()->NID) + " [label=EXIT];\n";
    }
    for (auto node : funcGraph.nodes())
    {
        for (const auto &succ : node->getSuccessors())
        {
            if (auto ce = dynamic_pointer_cast<CallEdge>(succ))
            {
                dotString += "\t" + to_string(ce->getSrc()->NID) + " -> " + to_string(ce->getSnk()->NID) + " [style=dashed,color=red];\n";
            }
            else if (auto re = dynamic_pointer_cast<ReturnEdge>(succ))
            {
                dotString += "\t" + to_string(re->getSrc()->NID) + " -> " + to_string(re->getSnk()->NID) + " [style=dashed,color=blue];\n";
            }
            else if (auto cond = dynamic_pointer_cast<ConditionalEdge>(succ))
            {
                dotString += "\t" + to_string(cond->getSrc()->NID) + " -> " + to_string(cond->getSnk()->NID) + " [style=dotted];\n";
            }
            else
            {
                dotString += "\t" + to_string(succ->getSrc()->NID) + " -> " + to_string(succ->getSnk()->NID) + ";\n";
            }
        }
    }
    dotString += "}";
    return dotString;
}

/*
    // write kernel file
    json outputJson;
    // valid blocks and block callers sections provide tik with necessary info about the CFG
    outputJson["ValidBlocks"] = std::vector<int64_t>();
    for (const auto &id : IDToBlock)
    {
        outputJson["ValidBlocks"].push_back(id.first);
    }
    for (const auto &bid : blockCallers)
    {
        outputJson["BlockCallers"][to_string(bid.first)] = bid.second;
    }
    // Entropy information
    outputJson["Entropy"] = map<string, map<string, uint64_t>>();
    outputJson["Entropy"]["Start"]["Entropy Rate"] = startEntropy;
    outputJson["Entropy"]["Start"]["Total Entropy"] = startTotalEntropy;
    outputJson["Entropy"]["Start"]["Nodes"] = startNodes;
    outputJson["Entropy"]["Start"]["Edges"] = startEdges;
    outputJson["Entropy"]["End"]["Entropy Rate"] = endEntropy;
    outputJson["Entropy"]["End"]["Total Entropy"] = endTotalEntropy;
    outputJson["Entropy"]["End"]["Nodes"] = endNodes;
    outputJson["Entropy"]["End"]["Edges"] = endEdges;

    // sequential ID for each kernel and a map from KID to sequential ID
    uint32_t id = 0;
    map<uint32_t, uint32_t> SIDMap;
    // average nodes per kernel
    float totalNodes = 0.0;
    // average blocks per kernel
    float totalBlocks = 0.0;
    for (const auto &kernel : kernels)
    {
        totalNodes += (float)kernel->nodes.size();
        totalBlocks += (float)kernel->getBlocks().size();
        for (const auto &n : kernel->nodes)
        {
            outputJson["Kernels"][to_string(id)]["Nodes"].push_back(n.NID);
        }
        for (const auto &k : kernel->getBlocks())
        {
            outputJson["Kernels"][to_string(id)]["Blocks"].push_back(k);
        }
        outputJson["Kernels"][to_string(id)]["Labels"] = std::vector<string>();
        outputJson["Kernels"][to_string(id)]["Labels"].push_back(kernel->Label);
        SIDMap[kernel->KID] = id;
        id++;
    }
    // now assign hierarchy to each kernel
    for (const auto &kern : kernels)
    {
        //auto entIDs = vector<uint32_t>();
        //auto exIDs  = vector<uint32_t>();
        // The entrances IDs we export have to refer to a block in the original bitcode explicitly, not an NID in our constructed graph here
        // Every ID up to the last in originalBlocks is past history
        // The last block represents the current block
        // The block that is outside the kernel is a neighbor of this node, 
        // The node that is still within the kernel that has an edge leading out of the kernel has its current block within the kernel and the next block outside the kernel
        // Thus we choose the last block in originalBlocks as the exit block
        // The same logic is applied to the entrance blocks, except the entrance block is the sink node of an edge that enters the kernel
        // This doesn't change what the logic is because the last node in the originalBlocks struct is still the current block
        //if( !kern->getExitBlocks(nodes, markovOrder).empty() )
        //{
        //    for( const auto& ex : kern->getExitBlocks(nodes, markovOrder) )
        //    {
        //        exIDs.push_back(ex);
        //    }
        //}
        //if( !kern->getEntranceBlocks(nodes, markovOrder).empty())
        //{
        //    for( const auto& en : kern->getEntranceBlocks(nodes, markovOrder) )
        //    {
        //        entIDs.push_back(en);
        //    }
        //}
        //outputJson["Kernels"][to_string(SIDMap[kern->KID])]["Entrances"] = vector<uint32_t>(entIDs);
        //outputJson["Kernels"][to_string(SIDMap[kern->KID])]["Exits"] = vector<uint32_t>(exIDs);
        outputJson["Kernels"][to_string(SIDMap[kern->KID])]["Children"] = vector<uint32_t>();
        outputJson["Kernels"][to_string(SIDMap[kern->KID])]["Parents"] = vector<uint32_t>();
    }
    for (const auto &kern : kernels)
    {
        // fill in parent category for children while we're filling in the children
        for (const auto &child : kern->getParentKernels())
        {
            outputJson["Kernels"][to_string(SIDMap[kern->KID])]["Children"].push_back(SIDMap[child]);
            outputJson["Kernels"][to_string(SIDMap[child])]["Parents"].push_back(SIDMap[kern->KID]);
        }
    }
    if (!kernels.empty())
    {
        outputJson["Average Kernel Size (Nodes)"] = float(totalNodes / (float)kernels.size());
        outputJson["Average Kernel Size (Blocks)"] = float(totalBlocks / (float)kernels.size());
    }
    else
    {
        outputJson["Average Kernel Size (Nodes)"] = 0.0;
        outputJson["Average Kernel Size (Blocks)"] = 0.0;
    }

    // performance intrinsics
    map<string, set<int64_t>> kernelBlockSets;
    for (const auto &kernel : outputJson["Kernels"].items())
    {
        if (outputJson["Kernels"].find(kernel.key()) != outputJson["Kernels"].end())
        {
            if (outputJson["Kernels"][kernel.key()].find("Blocks") != outputJson["Kernels"][kernel.key()].end())
            {
                auto blockSet = outputJson["Kernels"][kernel.key()]["Blocks"].get<set<int64_t>>();
                kernelBlockSets[kernel.key()] = blockSet;
            }
        }
    }

    auto prof = ProfileKernels(kernelBlockSets, SourceBitcode.get(), blockFrequencies);
    for (const auto &kernelID : prof)
    {
        outputJson["Kernels"][kernelID.first]["Performance Intrinsics"] = kernelID.second;
    }
    ofstream oStream(OutputFilename);
    oStream << setw(4) << outputJson;
    oStream.close();
}*/