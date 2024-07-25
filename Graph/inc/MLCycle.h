//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include "VirtualNode.h"
#include <deque>
#include <string>
#include <vector>

namespace Cyclebite::Graph
{
    constexpr uint64_t MIN_ANCHOR = 16;
    class MLCycle : public VirtualNode
    {
    public:
        uint32_t KID;
        std::string Label;
        MLCycle();

        /// @brief Compares this kernel to another kernel by measuring node differences
        ///
        /// If two kernels are the same, 1 will be returned
        /// If two kernels are completely different, 0 will be returned
        /// If two kernels share some nodes, (compare shared) / (this size) will be returned
        /// TODO: if this object fully overlaps with compare, but compare contains other blocks, this will say that we fully match when we actually don't. Fix that
        std::set<std::shared_ptr<ControlNode>, p_GNCompare> Compare(const MLCycle &compare) const;
        /// Returns true if any node in the kernel can reach every other node in the kernel. False otherwise
        bool FullyConnected() const;
        /// Returns the "path probability" of the Cycle, which is just the permutation of edge weights in the cycle
        float PathProbability() const;
        /// Returns the sum of entrances and exits of this cycle
        int EnExScore() const;
        /// Add a node to the subgraph of this cycle
        bool addNode(const std::shared_ptr<ControlNode> &newNode) override;
        /// Add nodes to the subgraph of this cycle
        void addNodes(const std::set<std::shared_ptr<ControlNode>, p_GNCompare> &newNodes) override;
        /// @brief Returns the child cycles of this cycle
        ///
        /// Child cycles are the cycles whose entrances (but not necessarily exits) are completely encapsulated by the body of this cycle
        const std::set<std::shared_ptr<MLCycle>, p_GNCompare> &getChildKernels() const;
        /// @brief Returns the parent cycles of this cycle
        ///
        /// Parent cycles are the cycles that completely encapsulate the entrances (but not necessarily the exits) of this cycle
        const std::set<std::shared_ptr<MLCycle>, p_GNCompare> &getParentKernels() const;
        /// Compares the subgraphs of two cycles - returns true if the KID of each cycle is identical
        inline bool operator==(const MLCycle &rhs) const;
        /// Removes a parent cycle from the current cycle by comparing NID of the input argument to an existing node in the parentKernels container
        void removeParentKernel(const std::shared_ptr<MLCycle>& parent);

    private:
        /// The children of this cycle, sorted by NID
        std::set<std::shared_ptr<MLCycle>, p_GNCompare> childKernels;
        /// The parents of this cycle, sorted by NID
        std::set<std::shared_ptr<MLCycle>, p_GNCompare> parentKernels;
        /// Class-wide counter to uniquely identify each kernel
        static uint32_t nextKID;
        /// Class-wide counter method called each time a new cycle is constructed
        static uint32_t getNextKID();
        void addParentKernel(std::shared_ptr<MLCycle> parent);
    };

    /// Allows for us to search a set of ControlNodes using an NID
    struct KCompare
    {
        using is_transparent = void;
        bool operator()(const std::shared_ptr<MLCycle> &lhs, const std::shared_ptr<MLCycle> &rhs) const
        {
            return lhs->KID < rhs->KID;
        }
        bool operator()(const std::shared_ptr<MLCycle> &lhs, uint64_t rhs) const
        {
            return lhs->KID < rhs;
        }
        bool operator()(uint64_t lhs, const std::shared_ptr<MLCycle> &rhs) const
        {
            return lhs < rhs->KID;
        }
    };
} // namespace Cyclebite::Graph