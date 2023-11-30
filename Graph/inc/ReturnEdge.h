//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include "CallEdge.h"
#include "ConditionalEdge.h"

namespace Cyclebite::Graph
{
    /* A conditional edge describes a single edge from one state to another that is based on a condition
     * It has the same structure as UnconditionalEdge because we are only interested in structuring the edges we observe, not the instructions underneath the dynamic execution
     * Thus, this structure is exactly like the UnconditionalEdge
     * But, we make a distinction between the two because the information of having a condition underneath this edge gives us more power to find important control sequences in the graph (like induction variables and branch prediction)
     */
    class ReturnEdge : public ConditionalEdge
    {
    public:
        ReturnEdge();
        ReturnEdge(const UnconditionalEdge &copy);
        ReturnEdge(uint64_t count, std::shared_ptr<ControlNode> sou, std::shared_ptr<ControlNode> sin, std::shared_ptr<CallEdge> call);
        const std::shared_ptr<CallEdge> &getCallEdge() const;

    private:
        std::shared_ptr<CallEdge> call;
    };
} // namespace Cyclebite::Graph