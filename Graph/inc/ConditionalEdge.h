#pragma once
#include "UnconditionalEdge.h"

namespace Cyclebite::Graph
{
    /* A conditional edge describes a single edge from one state to another that is based on a condition
     * It has the same structure as UnconditionalEdge because we are only interested in structuring the edges we observe, not the instructions underneath the dynamic execution
     * Thus, this structure is exactly like the UnconditionalEdge
     * But, we make a distinction between the two because the information of having a condition underneath this edge gives us more power to find important control sequences in the graph (like induction variables and branch prediction)
     */
    class ConditionalEdge : public UnconditionalEdge
    {
    public:
        ConditionalEdge();
        ConditionalEdge(const UnconditionalEdge &copy);
        ConditionalEdge(uint64_t count, std::shared_ptr<ControlNode> sou, std::shared_ptr<ControlNode> sin);
        void setWeight(uint64_t sum);
        float getWeight() const;
    };
} // namespace Cyclebite::Graph