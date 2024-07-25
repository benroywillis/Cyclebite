//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include "UnconditionalEdge.h"
#include <set>

namespace Cyclebite::Graph
{
    class CallGraphNode;
    class CallEdge;
    class CallGraphEdge : public UnconditionalEdge
    {
    public:
        CallGraphEdge();
        CallGraphEdge(std::shared_ptr<CallGraphNode> sou, std::shared_ptr<CallGraphNode> sin, std::set<std::shared_ptr<CallEdge>, GECompare> calls);
        /// @brief Returns the llvm::CallInstruction* pointers encapsulated by this CallGraphEdge
        const std::set<std::shared_ptr<CallEdge>, GECompare> &getCallEdges() const;
        const std::shared_ptr<CallGraphNode> getChild() const;
        const std::shared_ptr<CallGraphNode> getParent() const;

    private:
        /// @brief The Cyclebite::Graph::CallEdge objects encapsulated by this CallGraphEdge
        ///
        /// Each Cyclebite::Graph::CallEdge object represents one llvm::CallInstruction and contains information about its static and dynamic source and sink nodes
        std::set<std::shared_ptr<CallEdge>, GECompare> calls;
    };
} // namespace Cyclebite::Graph