//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include <llvm/Analysis/CallGraph.h>
#include <llvm/IR/BasicBlock.h>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <string>

namespace Cyclebite::Graph
{
    class ControlNode;
    class DataValue;
    class Inst;
    class ControlBlock;
    class MLCycle;
    class UnconditionalEdge;
    class CallEdge;
    class Graph;
    class ControlGraph;
    class DataGraph;
    class CallGraph;
    struct GNCompare;
    struct p_GNCompare;
    struct KCompare;
    struct GECompare;
    // maps the dynamic pass IDs to their LLVM objects
    extern std::map<int64_t, const llvm::BasicBlock *> IDToBlock;
    extern std::map<int64_t, const llvm::Value *> IDToValue;
    // maps source basic block IDs to sink basic block IDs whose control edge is a function call
    extern std::map<int64_t, std::vector<int64_t>> blockCallers;
    /// maps basic block IDs to their user-annotated label (deprecated)
    extern std::map<int64_t, std::map<std::string, int64_t>> blockLabels;
    /// Holds the basic blocks whose executions contain a thread launch
    extern std::set<int64_t> threadStarts;
    // maps a block ID list (that represents the markov chain state, which could have arbitrary order number) to an llvm::BasicBlock ID which the node represents
    // instantiated in cartographer/new/IO.cpp
    extern std::map<std::vector<uint32_t>, uint64_t> NIDMap;
    // maps an llvm instruction to its corresponding libGraph datanode, initialized in Graph/IO.cpp:BuildDFG()
    extern std::map<const llvm::Value*, const std::shared_ptr<DataValue>> DNIDMap;
    // maps an llvm basic block to its corresponding libGraph controlnode, initialized in Graph/IO.cpp:BuildDFG()
    extern std::map<const llvm::BasicBlock*, const std::shared_ptr<ControlBlock>> BBCBMap;
    struct EntropyInfo
    {
        double start_entropy_rate;
        double start_total_entropy;
        uint32_t start_node_count;
        uint32_t start_edge_count;
        double end_entropy_rate;
        double end_total_entropy;
        uint32_t end_node_count;
        uint32_t end_edge_count;
    };
    /// Initializes the IDToBlock and IDToValue global maps
    /// The input module must be annotated using Cyclebite::Util::Annotate before this method is called
    /// After annotation, this method walks through the static blocks and instructions, and maps their IDs to their object pointers
    void InitializeIDMaps(llvm::Module *M);
    /// @brief Reads the recorded basic block information from the Markov Profile into a map
    /// 
    /// Initializes the following three global maps
    /// BlockCallers - maps source basic block IDs to sink basic block IDs whose control edge is a function call
    /// BlockLabels  - maps basic block IDs to their user-annotated label (deprecated)
    /// ThreadStarts - contains the basic block IDs whose instructions contain a thread launch
    /// @param BlockInfo    A string pointing to the json file to be read (usually BlockInfo_<application>.json)
    void ReadBlockInfo(const std::string &BlockInfo);
    /// Calculates the information entropy of the probability weights in the graph
    /// This is a measure of how random the control flow of the graph is - a number close to 1 indicates lots of randomness (i.e., each edge weight is equal to all others), a number close to 0 indicates determinism (some edge weights are much greater than others)
    double TotalEntropy(const std::set<std::shared_ptr<ControlNode>, p_GNCompare> &nodes);
    /// Calculates the stationary entropy of the graph - this is a normalized value compared to the total entry (normalized according to how many nodes are in the graph)
    double EntropyCalculation(const std::set<std::shared_ptr<ControlNode>, p_GNCompare> &nodes);
    /// @brief Generates neat structures for all the dynamic information imported for a given application
    ///
    /// Steps:
    /// 1. buildCFG - reads the input state transition table and transforms it into a markov chain
    /// 2. UpgradeEdges - lift special objects (CallEdge, ConditionalEdge, ImaginaryNode, etc) into their respective classes
    /// 3. PatchFunctionEdges - inject calledges where the function pointers exist, and build calledges where they are implied (see PatchFunctionEdges for specifics of empty functions calling live ones)
    /// 4. AddImaginaryEdges - defines the known boundaries around the control graph - where main starts and where the program terminated all get their own imaginary objects
    /// 5. getDynamicCallGraph - constructs a callgraph for the program using the injected dynamic information from the program
    /// 6. removeTailHeadCalls - patches corner cases where empty functions call live ones, and it appears as though that live function calls itself tail-head-tail-head (which is not what the program did)
    /// 7. IO - print some graphs that show what we have build in graphviz
    /// 8. Checks - make sure our control graph and call graph make sense
    /// @param cg               The control graph that will hold the Markov Control Graph of the input program. This object is filled by this method.
    /// @param dynamicCG        The control graph that will hold the dynamic call graph of the program. This object is filled by this method.
    /// @param filePath         The path to the state transition table file (<project>.bin)
    /// @param sourceBitcode    A pointer to the llvm::Module of the input program
    /// @param staticCG         A call graph that holds the call graph that comes from the static llvm representation (comes from Cyclebite::Graph::GenerateCallGraph(llvm::CallGraph(*sourceBitcode))
    /// @param blockCallers     A map that draws edges between the source and sink basic blocks of function pointers (instantiated with ReadBlockInfo)
    /// @param threadStarts     A set of basic block IDs that denote the states that launch threads (instantiated with ReadBlockInfo)
    /// @param IDToBlock        A map that draws edges between uint64_t ID numbers assigned by Cyclebite::Util::Annotate() and llvm::BasicBlock*s
    /// @param HotCodeDetection Flag for enabling hot code detection. This flag enables BuildCFG to carry out a hot code/hot loop analysis of the program by reducing its markov chain to length 0 and designating all basic blocks that account for 95% of all state frequency in the program as tasks. HotLoop analysis then designates all static loops with at least one hot basic block as a task.
    void getDynamicInformation( Cyclebite::Graph::ControlGraph& cg, 
                                Cyclebite::Graph::CallGraph& dynamicCG, 
                                const std::string& filePath, 
                                const std::unique_ptr<llvm::Module>& SourceBitcode, 
                                const llvm::CallGraph& staticCG, 
                                const std::map<int64_t, std::vector<int64_t>>& blockCallers, 
                                const std::set<int64_t>& threadStarts, 
                                const std::map<int64_t, const llvm::BasicBlock*>& IDToBlock, 
                                bool HotCodeDetection );
    /// @brief Transforms the state transition table file (<application>.bin) into a markov chain
    ///
    /// Steps:
    /// 1. Read input binary file - reads the binary file in a specific way
    /// 2. Everything in graph after step 1 is an unconditional edge - it will be upgraded later with UpgradeEdges()
    /// @param graph            The graph that will hold the Markov Control Graph
    /// @param filename         A path to the input state transition table (<application>.bin)
    /// @param HotCodeDetection Enables hot code/hot loop detection. BuildCFG will carry out a hot code/hot loop analysis of the program that reduces the markov chain to length 0 and designates all basic blocks that account for 95% of all state frequency in the program as tasks. HotLoop analysis designates all static loops with at least one hot basic block as a task.
    int BuildCFG(Graph &graph, const std::string &filename, bool HotCodeDetection);
    /// @brief Builds the dynamic call graph for the input program
    ///
    /// The dynamic call graph is a complete call graph of the application that doesn't include holes caused by function pointers
    /// Each node points to a function that was proven to have executed during the execution profile (though the function may be empty)
    /// @param mod      The static llvm::module of the target application. Its functions and their interconnections are used to build a preliminary representation of the dynamic call graph.
    /// @param graph    The markov control graph of the application. Its basic blocks are used to measure which functions were used in the execution profile
    /// @param blockCallers     A map of basic block IDs in which caller BBs are keys and callee BBs are values. Used to resolve function pointers
    /// @param IDToBlock        Maps basic block IDs to basic block pointers         
    const Cyclebite::Graph::CallGraph getDynamicCallGraph( llvm::Module *mod, 
                                                           const Graph &graph, 
                                                           const std::map<int64_t, std::vector<int64_t>> &blockCallers, 
                                                           const std::map<int64_t, const llvm::BasicBlock *> &IDToBlock);
    /// @brief Implements checks on the call graph to make sure the call graph is sane
    ///
    /// Checks:
    /// 1. CallEdge objects in the Markov Control Graph and CallGraphEdges in the DCG should agree
    /// 2. All alive call edges are represented in the resulting call graph
    /// @param SCG      Static call graph. Comes from llvm::CallGraph staticCG(llvm::Module& *SourceBitcode)
    /// @param DCG      Dynamic call graph. Comes from getDynamicCallGraph()
    /// @param dynamicGraph The Markov control graph of the application
    /// @param IDToBlock    Maps basic block IDs to basic block pointers. IDs come from Cyclebite::Util::Annotate() and IDToBlock is initialized by InitializeIDMaps()
    void CallGraphChecks(const llvm::CallGraph &SCG, const Cyclebite::Graph::CallGraph &DCG, const Graph &dynamicGraph, const std::map<int64_t, const llvm::BasicBlock *> &IDToBlock);
    /// @brief Builds a directed graph of Cyclebite::Graph::ControlBlock's and their interconnections, each containing Cyclebite::Graph::DataValue's and their interconnections
    ///
    /// This is an emulation of LLVM bitcode, where all objects are based on their corresponding objects in llvm bitcode
    /// Cyclebite::Graph::ControlBlock -> llvm::BasicBlock
    /// Cyclebite::Graph::DataValue    -> llvm::Value
    /// @param programFlow      Contains the resulting dataflow graph. This container has the result of this method after execution is finished
    /// @param graph            The dataflow graph of instrructions built by this method. This container has a result of this method after execution is finished
    /// @param SourceBitcode    A pointer to the static module of the application
    /// @param dynamicCG        The dynamic call graph of the application
    /// @param blockToNode      A map pointing from llvm::BasicBlock to its corresponding control node
    /// @param IDToBlock        A map pointing from basic block ID to llvm::BasicBlock*
    void BuildDFG( std::set<std::shared_ptr<ControlBlock>, p_GNCompare> &programFlow, 
                   DataGraph &graph, 
                   const std::unique_ptr<llvm::Module>& SourceBitcode, 
                   const Cyclebite::Graph::CallGraph& dynamicCG, 
                   const std::map<int64_t, std::shared_ptr<ControlNode>> &blockToNode, 
                   const std::map<int64_t, const llvm::BasicBlock*>& IDToBlock);
    /// @brief A method that collects PERFMON data for each extracted task and assembles them into a map
    ///
    /// This method is deprecated and will be removed in a future release.
    std::map<std::string, std::map<std::string, std::map<std::string, int>>> ProfileKernels( const std::map<std::string, std::set<int64_t>> &kernels, 
                                                                                             llvm::Module *M, 
                                                                                             const std::map<int64_t, uint64_t> &blockCounts );
    /// @brief Finds the original blocks that represent a node in the simplified markov control graph
    ///
    /// When the tasks of the application are exported, their entrance and exit edges must be explicitly named to allow for the localization of their instances in the Epoch Profile
    /// But, the transformed MCG contains virtual nodes that map to possibly many original basic blocks in the static code (and therefore do not represent the source of a control edge in the static program)
    /// To fix this problem, this method walks the blocks represented by a given virtual node and extracts the blocks that likely form a real control edge in the original static program
    /// Generally, it just picks the leading-edge basic block and returns that as the "original" block (which actually forms a useful source for a control edge)
    std::set<std::pair<int64_t, int64_t>> findOriginalBlockIDs(const std::shared_ptr<UnconditionalEdge>& edge);
    /// @brief Dumps a json of the tasks that were collected by the cartographer
    void WriteKernelFile( const ControlGraph &graph, 
                          const std::set<std::shared_ptr<MLCycle>, KCompare> &kernels, 
                          const std::map<int64_t, const llvm::BasicBlock *> &IDToBlock, 
                          const std::map<int64_t, std::vector<int64_t>> &blockCallers, 
                          const EntropyInfo &info, 
                          const std::string &OutputFileName, 
                          bool hotCode = false );
    /// @brief Returns a string representing the dot file of the input graph. 
    ///
    /// Nodes in the .dot are nodes in the input graph, annotated with their NID
    /// Edges in the .dot are edges in the input graph, annotated with their probability
    std::string GenerateDot(const Graph &graph, bool original = false);
    /// @brief Returns a string of the  dot of the union of the nodes in each input, and highlights the covered nodes
    std::string GenerateCoverageDot(const std::set<std::shared_ptr<ControlNode>, p_GNCompare> &coveredNodes, const std::set<std::shared_ptr<ControlNode>, p_GNCompare> &uncoveredNodes);
    /// @brief Returns a string of the dot file that represents the nodes in the input "nodes" set, and highlights the "kernels" in that graph
    std::string GenerateTransformedSegmentedDot(const std::set<std::shared_ptr<ControlNode>, p_GNCompare> &nodes, const std::set<std::shared_ptr<MLCycle>, KCompare> &kernels, int markovOrder);
    /// @brief Dumps a file called "DynamicCoverage.dot" that highlights the dynamically-exercised nodes in the static control graph
    void GenerateDynamicCoverage(const std::set<std::shared_ptr<ControlNode>, p_GNCompare> &dynamicNodes, const std::set<std::shared_ptr<ControlNode>, p_GNCompare> &staticNodes);
    /// Generates a Cyclebite::Graph::CallGraph of the static module of an input application
    ControlGraph GenerateStaticCFG(llvm::Module *M);
    /// @brief Returns a string of a .dot file of the input data graph. 
    ///
    /// Each ControlBlock is highlighted and labeled
    /// Each control edge is a dashed line annotated with its probability
    /// Each DataValue is a node in the graph
    /// Each solid line is a data dependency between DataValue's
    std::string GenerateDataDot(const std::set<std::shared_ptr<DataValue>, p_GNCompare> &nodes);
    std::string GenerateBBSubgraphDot(const std::set<std::shared_ptr<ControlBlock>, p_GNCompare> &BBs);
    /// @brief Returns a string of the .dot file of the input graph, and highlights subgraph in blue
    std::string GenerateHighlightedSubgraph(const Graph &graph, const Graph &subgraph);
    /// @brief Returns a string of the .dot file of the input funcGraph and highlights its entrance edge
    std::string GenerateFunctionSubgraph(const Graph &funcGraph, const std::shared_ptr<CallEdge> &entrance);
    /// @brief Returns a string of the .dot file of the llvm call graph
    std::string GenerateCallGraph(const llvm::CallGraph &CG);
    /// @brief Returns a string of the dot file of the input dynamic call graph 
    std::string GenerateCallGraph(const Cyclebite::Graph::CallGraph &CG);
} // namespace Cyclebite::Graph