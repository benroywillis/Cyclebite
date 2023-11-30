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
        /// Meant to be constructed from a new block description in the input binary file
        ~CallGraphNode() = default;
        const llvm::Function *getFunction() const;
        const std::set<std::shared_ptr<CallGraphEdge>, GECompare> getChildren() const;
        const std::set<std::shared_ptr<CallGraphEdge>, GECompare> getParents() const;

    private:
        /// This function is guaranteed to be non-empty if it is defined
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