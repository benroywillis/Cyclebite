//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include "GraphNode.h"
#include "llvm/IR/Function.h"

namespace Cyclebite::Graph
{
    class CallGraphEdge;
    class CallGraphNode : public GraphNode
    {
    public:
        CallGraphNode(const llvm::Function *F);
        ~CallGraphNode() = default;
        /// @brief Returns the llvm::Function this node maps to
        const llvm::Function *getFunction() const;
        /// @brief Returns the edge(s) that lead to callees of this caller
        /// Thus the sink nodes of the returned edges are the callees of this caller
        const std::set<std::shared_ptr<CallGraphEdge>, GECompare> getChildren() const;
        /// @brief Returns the edge(s) that lead to the callers of this callee
        /// Thus, the source nodes of the returned edges are the callers of this callee
        const std::set<std::shared_ptr<CallGraphEdge>, GECompare> getParents() const;

    private:
        /// May be an empty function
        const llvm::Function *f;
    };

    struct CGNCompare
    {
        using is_transparent = void;
        bool operator()(const std::shared_ptr<CallGraphNode> &lhs, const std::shared_ptr<CallGraphNode> &rhs) const
        {
            return lhs->getFunction() < rhs->getFunction();
        }
        bool operator()(const llvm::Function *f, const std::shared_ptr<CallGraphNode> &rhs) const
        {
            return f < rhs->getFunction();
        }
        bool operator()(const std::shared_ptr<CallGraphNode> &lhs, const llvm::Function *f) const
        {
            return lhs->getFunction() < f;
        }
    };
} // namespace Cyclebite::Graph