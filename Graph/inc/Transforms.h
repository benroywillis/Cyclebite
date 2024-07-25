//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include <llvm/Analysis/CallGraph.h>
#include <llvm/IR/BasicBlock.h>
#include <map>
#include <set>

namespace Cyclebite::Graph
{
    class GraphNode;
    class ControlNode;
    class VirtualNode;
    class CallGraphNode;
    class MLCycle;
    class Graph;
    class CallGraph;
    class ControlGraph;
    struct p_GNCompare;
    struct KCompare;

    /// Keeps track of all dead blocks in the input program
    extern std::set<const llvm::BasicBlock *> deadCode;
    /// @brief Implements a series of check on the transformed Markov Control Graph to ensure correctness after each iteration of transforms
    ///
    /// Checks:
    /// 1. The graph cannot be empty
    /// 2. All edges that exist within node pred/succ containers must exist within the graph
    /// 3. All nodes in the graph must be reachable
    /// 4. All nodes in the graph must be reverse-reachable from at least one program terminator
    /// 5. For each node in the graph, all outgoing edge weights must sum to 1
    void Checks(const ControlGraph &transformed, std::string step, bool segmentation = false);
    /// @brief Maps an llvm basic block pointer to a Cyclebite::Graph::GraphNode that represents it
    ///
    /// This operation is useful when it's convenient to go from the static LLVM IR of an application to its representation in Cyclebite's Markov Control Graph (MCG)
    /// However this may not be straightforward once the MCG is transformed to a reasonable degree
    /// Thus, this method does its best to map a basic block to its representative node in the MCG passed into the method through the "graph" arg
    /// @param graph    The current MCG
    /// @param block    The LLVM basic block to map to a Cyclebite GraphNode
    /// @param NIDMap   Maps a vector of LLVM basic block IDs to a Cyclebite GraphNode NID
    std::shared_ptr<GraphNode> BlockToNode(const Graph &graph, const llvm::BasicBlock *block, const std::map<std::vector<uint32_t>, uint64_t> &NIDMap);
    /// @brief Maps a Cyclebite GraphNode to an LLVM IR basic block
    ///
    /// This method is useful when it's convenient to go from the Cyclebite Markov Control Graph (MCG) to the LLVM IR 
    /// This may not be a straightforward mapping after the MCG has been transformed to a sufficient degree
    /// When the input "node" sits on top of a subgraph, this method returns the basic block whose outgoing edge(s) exit that subgraph
    const llvm::BasicBlock *NodeToBlock(const std::shared_ptr<ControlNode> &node, const std::map<int64_t, const llvm::BasicBlock *> &IDToBlock);
    /// @brief Reverses the transformations done onto the input Markov Control Graph (MCG)
    ///
    /// This method is useful to verify that transformations have been tidy in grouping nodes into virtual nodes
    /// Otherwise it is not used in practice and is not actively maintained
    void reverseTransform(Graph &graph);
    /// @brief Reverses the cyclical subgraph transformations done onto the input Markov Control Graph (MCG)
    ///
    /// This method is useful to verify the transformations aren't doing bad things
    /// Otherwise it is not used in practice and is not maintained
    /// @retval     A Markov Control Graph that does not contain any VirtualNodes or VirtualEdges
    ControlGraph reverseTransform_MLCycle(const ControlGraph& graph);
    /// @brief Checks for indirect recursion within the bodies of multiple functions
    ///
    /// This method is used to find cases of indirection recursion within the application in order to handle that case when inlining a given function at its call sites
    /// Indirect recursion is the phenomenon of functions calling themselves through other functions
    /// Think of a subgraph within the function call graph of the application that forms a cycle
    /// Indirect recursion has major implications on how the subgraph of this function should be inlined at its call sites
    ///
    /// Method: uses Dijkstra's algorithm to find a cycle within the function call graph
    /// 
    /// @param graph    The call graph that holds the target function
    /// @param node     The function whose indirect recursion should be found, if it exists
    /// @retval         True if the function has indirect recursion, false otherwise
    bool hasIndirectRecursion(const Cyclebite::Graph::CallGraph &graph, const std::shared_ptr<Cyclebite::Graph::CallGraphNode> &node);
    /// @brief Returns true if the given CallGraphNode is indirect recursive, false otherwise
    ///
    /// This method is helpful to find a cycle within the function subgraph, but is not as robust as its graph-accepting overload
    /// Method: Uses a depth-first search to find a "backedge" in the function call graph (which indicates a cycle)
    bool hasIndirectRecursion(const llvm::CallGraphNode *node);
    /// @brief Returns true if the input function "src" calls itself from within its own function body, false otherwise
    ///
    /// Method: uses Dikjstra's on the function call graph to find a cycle with the input "src" and itself - if the returned cycle has length 1 (and refers to "src"), the function returns true
    bool hasDirectRecursion(const Cyclebite::Graph::CallGraph &graph, const std::shared_ptr<Cyclebite::Graph::CallGraphNode> &src);
    /// @brief Returns true if the input CallGraphNode is direct recursive, false otherwise
    ///
    /// Method: searches the input callgraphnode's outgoing edges for an edge that points to itself - if this edge is found, it returns true. False otherwise.
    bool hasDirectRecursion(const llvm::CallGraphNode *node);
    /// @brief Transforms the input subgraph into a virtual node
    ///
    /// A virtual node represents a subgraph of nodes within itself
    /// This is the fundamental method in which Cyclebite simplified a Markov Control Graph (MCG)
    /// @param graph    The MCG that contains the target "subgraph" to virtualize. "graph" will be transformed after this method is complete.
    /// @param VN       The virtual node to represent the virtualized subgraph in "graph". This object's members and methods will hold the result of this method
    /// @param subgraph Should contain all nodes and edges of the target subgraph to virtualize - should NOT contain the entrance and exit edges of that subgraph
    void VirtualizeSubgraph(Graph &graph, std::shared_ptr<VirtualNode> &VN, const ControlGraph &subgraph);
    /// @brief Inlines the input function's subgraph at each of its callsites
    ///
    /// Steps:
    /// 1. Find all functions that contain multiple (live) call sites - these functions should be inlined
    /// 2. Schedule: inline functions in reverse-hierarchical order (child-most function calls first). This ensures parent function subgraphs contain inlined calls before they themselves are inlined
    /// 3. Inline: for each function in the schedule, inline at each of its callsites (which will handle three cases: indirect recursion, direct recursion, non-recursive)
    /// 4. Check: between each function inline, make sure the last transforms produced a valid graph
    /// @param graph        The MCG whose functions need to be inlined. This container will be transformed after this method is complete.
    /// @param dynamicCG    The dynamic call graph of the program whose MCG is represented by "graph"
    void VirtualizeSharedFunctions(ControlGraph &graph, const Cyclebite::Graph::CallGraph &dynamicCG);
    /// @brief Virtualizes the subgraphs of the localized cycles in "newKernels"
    /// 
    /// Each input kernel subgraph is virtualized (in similar ways to the simplifying transforms) by this method
    /// @param newKernels   The MLCycle objects whose subgraphs should be virtualized
    /// @param graph        The simplified MCG whose graph will be transformed by this method
    std::vector<std::shared_ptr<MLCycle>> VirtualizeKernels(std::set<std::shared_ptr<MLCycle>, KCompare> &newKernels, ControlGraph &graph);
    /// A method that checks whether every node in the input set has outgoing edges whose weights sum to one. Throws an exception if at least one node doesn't pass
    void SumToOne(const std::set<std::shared_ptr<GraphNode>, p_GNCompare> &nodes);
    /// @brief Transforms serial chains of nodes into virtual nodes
    /// 
    /// Method: simply walk the edges of the graph starting from "sourceNode", each time recording which node we touch, until we do not have a serial chain of nodes anymore
    /// @param sourceNode   The node that starts the search for a serial subgraph
    /// @retval             A ControlGraph that contains all nodes and edges that should be virtualized into a single node
    ControlGraph TrivialTransforms(const std::shared_ptr<ControlNode> &sourceNode);
    /// @brief Transforms subgraphs that enter through a single node and exit through a single node
    ///
    /// Unlike "FanInFanOutTransform" which supports subgraphs up to 100 nodes, this method only supports subgraphs that contain three steps: src -> mess -> sink
    /// Thus, it is an abridged version of FanInFanOutTransform
    /// @param graph    The Markov Control Graph to transform
    /// @param source   The node that should start this search. If source is not found to start an eligible subgraph, the method returns an empty graph
    /// @retval         A subgraph that should be virtualized. If source did not start a valid subgraph, this structure is empty
    ControlGraph BranchToSelectTransforms(const ControlGraph &graph, const std::shared_ptr<ControlNode> &source);
    /// @brief Evaluates a subgraph for its entrances and exits, and returns true if the entrance and exit are the bottlenecks of the subgraph
    ///
    /// @param subgraph Input subgraph to evaluate. This subgraph cannot contain any cycles, and there must be a path between source and sink. This parameter is passed by reference and may be manipulated if the function returns true
    /// @param source   The intended source node of the subgraph. A source node of the subgraph should have all its predecessors outside the subgraph and all its successors within the subgraph
    /// @param sink     The intended sink node of the subgraph. A sink node of the subgraph should have all its predecessors within the subgraph and all its successors outside
    /// @retval         True if the input subgraph can only be entered into through source and only exited through sink
    bool FanInFanOutTransform(ControlGraph &subgraph, const std::shared_ptr<ControlNode> &source, const std::shared_ptr<ControlNode> &sink);
    /// @brief Finds a subgraph of nodes starting at "source" that bottleneck into source and out of an unknown "sink" node
    ///
    /// This method uses edge coloring to find subgraphs that must enter through "source" and must exit through an unknown "sink" node, with no cycles in between
    /// See the method source code for technical details on its implementation
    /// Note: this method stops if the subgraph being explored grows larger than 100 nodes (if a target subgraph is larger than 100 nodes, simplifying transforms should reduce its size under this parameter)
    /// @param subgraph     Contains the bottlenecking subgraph if one is found, empty otherwise
    /// @param source       The node to start the search.
    /// @retval             The sink node of the graph in "subgraph" if a valid subgraph is found. Nullptr otherwise
    const std::shared_ptr<ControlNode> FindNewSubgraph(ControlGraph &subgraph, const std::shared_ptr<ControlNode> &source);
    /// @brief Wrapper function to apply all simplifying transforms to a Markov Control Graph
    ///
    /// This method iteratively applied simplifying transforms to the graph till no transform opportunities are found - thus, some transforms may enable other after they complete
    /// Between each iteration, this method applies checks to the MCG to ensure the last iteration of transforms produced a valid MCG
    /// This method is applied both before cycles are localized and during localization - thus, cycle localization may permit new transformations after they are virtualized into single nodes
    /// @param graph        The container that should hold a Markov Control Graph, and will contain a simplified Markov Control Graph after the execution of this method
    /// @param dynamicCG    The dynamical call graph of the MCG in "graph"
    /// @param segmentation A flag that tells this method whether the MCG is being simplified for the first time, or if this method is applied between cycle localization passes (it changes the checks implemented after each iteration)
    void ApplyCFGTransforms(ControlGraph &graph, const Cyclebite::Graph::CallGraph &dynamicCG, bool segmentation = false);
    /// @brief Iteratively finds cyclical subgraphs till not valid subgraphs are left
    ///
    /// Method: apply Dijkstra's to each node in the simplified MCG
    /// When cycles are found, ensure they are localized in the correct ordering by applying EnExCount and PathProbability methods
    /// Between each iteration of ML cycle finding, apply simplifying transforms to transform any subgraphs that just became eligible now that its cycle is gone
    /// Apply checks to the MCG between each iteration to ensure the resulting MCG is valid
    /// @param graph            A simplified MCG whose nodes and edges will be transformed and hold the result of this method
    /// @param dynamicCG        The dynamic call graph of the MCG
    /// @param applyTransforms  Flag to turn off transforms, if not desired
    std::set<std::shared_ptr<MLCycle>, KCompare> FindMLCycles(ControlGraph &graph, const Cyclebite::Graph::CallGraph &dynamicCG, bool applyTransforms = false);
    /// @brief Printer method that finds statistics on the input call graph and prints them to the screen
    void FindAllRecursiveFunctions(const llvm::CallGraph &CG, const Graph &graph, const std::map<int64_t, const llvm::BasicBlock *> &IDToBlock);
    /// @brief Printer method that prints statistics on the input call graph 
    void FindAllRecursiveFunctions(const Cyclebite::Graph::CallGraph &CG, const Graph &graph, const std::map<int64_t, const llvm::BasicBlock *> &IDToBlock);
} // namespace Cyclebite::Graph