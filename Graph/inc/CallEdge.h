#pragma once
#include "ConditionalEdge.h"
#include "ControlNode.h"
#include "llvm/IR/Function.h"
#include <set>

namespace TraceAtlas::Graph
{
    /// Contains information about a CallEdge return edge (which is implicit to the function call convention in C/C++ but not present in the state transition tables)
    struct Returns
    {
        /// Dynamic node whose underlying basic blocks contains the function call instruction
        std::shared_ptr<ControlNode> callerNode;
        /// Dynamic nodes whoses underlying basic blocks contain return instructions of the called function
        std::set<std::shared_ptr<ControlNode>, p_GNCompare> staticExits;
        /// Edges that point from a member of returnNodes to callerNode. These edges does not exist in the dynamic profile graph.
        std::set<std::shared_ptr<UnconditionalEdge>, GECompare> staticRets;
        /// Dynamic nodes whose underlying basic blocks are the (possible) successors of the callerNode
        std::set<std::shared_ptr<ControlNode>, p_GNCompare> dynamicExits;
        /// Edges that point from a member of returnNode to a member of dynamicExits. These edges exist in the dynamic profile graph.
        std::set<std::shared_ptr<UnconditionalEdge>, GECompare> dynamicRets;
        /// Nodes that belong to the function being called, according to the source code
        std::set<std::shared_ptr<ControlNode>, p_GNCompare> functionNodes;
        /// LLVM function called at this point
        const llvm::Function *f;
    };

    class CallEdge : public ConditionalEdge
    {
    public:
        Returns rets;
        CallEdge();
        CallEdge(const UnconditionalEdge &copy);
        CallEdge(uint64_t freq, std::shared_ptr<ControlNode> sou, std::shared_ptr<ControlNode> sin);
    };
} // namespace TraceAtlas::Graph