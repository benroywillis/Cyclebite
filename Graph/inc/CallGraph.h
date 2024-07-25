//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include "CallGraphEdge.h"
#include "CallGraphNode.h"
#include "Graph.h"

namespace Cyclebite::Graph
{
    /// Cyclebite::Graph::CallGraph objects encapsulate the call graph of a program
    /// Each node is a function that is guaranteed to be non-empty
    /// Each edge is a "summary" of the call edges that exist between caller (source) and callee (sink) (that is, each edge maps to one or more call instructions in the program)
    class CallGraph : public Graph
    {
    public:
        CallGraph();
        /// @brief Constructs a graph of functions and call edges. Nodes are functions (possibly empty) and edges point from caller to callee.
        CallGraph(const std::set<std::shared_ptr<CallGraphNode>, CGNCompare> &nodeSet, const std::set<std::shared_ptr<CallGraphEdge>, GECompare> &edgeSet);
        const std::set<std::shared_ptr<CallGraphNode>, CGNCompare> getCallNodes() const;
        /// @brief Returns whether this llvm::Function is in the call graph. False otherwise. 
        bool find(const llvm::Function *f) const;
        /// @brief Returns the pointer to the node that represents this function.
        /// If there is no node that contains this function, a CyclebiteException is thrown
        /// If more than one node contains this function, a random node is returned
        const std::shared_ptr<CallGraphNode> &operator[](const llvm::Function *f) const;
        void addNode(const std::shared_ptr<CallGraphNode> &a);
        void addNodes(const std::set<std::shared_ptr<CallGraphNode>, CGNCompare> &nodes);
        void removeNode(const std::shared_ptr<CallGraphNode>& r);
        /// @brief Returns the "main" function of the program
        /// The main function has no incoming edges, only outgoing ones
        /// If more than one main node is found, a CyclebiteException is thrown
        /// If zero nodes pass the criteria for a main node, a CyclebiteException is thrown 
        const std::shared_ptr<CallGraphNode> getMainNode() const;
    private:
        std::set<std::shared_ptr<CallGraphNode>, CGNCompare> CGN;
    };
} // namespace Cyclebite::Graph