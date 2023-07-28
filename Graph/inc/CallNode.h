#pragma once
#include "Inst.h"
#include "ControlBlock.h"

namespace Cyclebite::Graph
{
    class CallNode : public Inst
    {
    public:
        CallNode( const llvm::Instruction* inst, const std::set<std::shared_ptr<ControlBlock>, p_GNCompare>& dests );
        // upgrade constructor
        CallNode( const Inst *upgrade, const std::set<std::shared_ptr<ControlBlock>, p_GNCompare>& dests );
        const std::set<std::shared_ptr<ControlBlock>, p_GNCompare>& getDestinations() const;
    private:
        // Destinations of a callnode are the possible places this call instructions can go (think function pointer that calls objects with the same args)
        std::set<std::shared_ptr<ControlBlock>, p_GNCompare> destinations;
    };
} // namespace Cyclebite::Graph