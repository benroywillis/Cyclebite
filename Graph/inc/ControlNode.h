//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include "GraphNode.h"

namespace Cyclebite::Graph
{
    class UnconditionalEdge;
    class ControlNode : public GraphNode
    {
    public:
        /// Holds the blockIDs that originally constructed this node from the profile
        /// The blocks are in order from beginning to end, least recent to most recent
        std::vector<uint32_t> originalBlocks;
        /// BBIDs from the source bitcode that are represented by this node
        /// Each key is a member BBID and its value is the basic block its unconditional edge points to
        /// If a key maps to itself, there is no edge attached to this block
        std::set<int64_t> blocks;
        ControlNode();
        /// Meant to be constructed from a new block description in the input binary file
        ~ControlNode() = default;
        /// Incoming edges
        const std::set<std::shared_ptr<UnconditionalEdge>, GECompare> getPredecessors() const;
        /// Outgoing edges
        const std::set<std::shared_ptr<UnconditionalEdge>, GECompare> getSuccessors() const;
        void addPredecessor(std::shared_ptr<UnconditionalEdge> newEdge);
        void removePredecessor(std::shared_ptr<UnconditionalEdge> oldEdge);
        void addSuccessor(std::shared_ptr<UnconditionalEdge> newEdge);
        void removeSuccessor(std::shared_ptr<UnconditionalEdge> oldEdge);
        /// @brief Adds an underlying llvm::BasicBlock::ID that this node represents
        /// 
        /// Each node comes from an llvm::basicblock in the original program
        /// Each basic block gets its own ID from the Cyclebite::Profile::Passes::Annotate pass
        /// Before MCG transformation, each ControlNode maps to exactly one llvm::basicblock
        /// After transformation, a ControlNode may map to many llvm::basicblocks (or an entire subgraph of them)
        bool addBlock(int64_t newBlock);
        void addBlocks(const std::set<int64_t> &newBlocks);
        /// Merges the blocks and originalBlocks of a successor node
        bool mergeSuccessor(const ControlNode &succ);
    };
} // namespace Cyclebite::Graph