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
        /// Returns the probability that this kernel keeps recurring vs. exiting
        float PathProbability() const;
        int EnExScore() const;
        bool addNode(const std::shared_ptr<ControlNode> &newNode) override;
        void addNodes(const std::set<std::shared_ptr<ControlNode>, p_GNCompare> &newNodes) override;
        const std::set<std::shared_ptr<MLCycle>, p_GNCompare> &getChildKernels() const;
        const std::set<std::shared_ptr<MLCycle>, p_GNCompare> &getParentKernels() const;
        inline bool operator==(const MLCycle &rhs) const;
        void removeParentKernel(const std::shared_ptr<MLCycle>& parent);

    private:
        /// set of KIDs that point to child kernels of this kernel
        std::set<std::shared_ptr<MLCycle>, p_GNCompare> childKernels;
        std::set<std::shared_ptr<MLCycle>, p_GNCompare> parentKernels;
        static uint32_t nextKID;
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