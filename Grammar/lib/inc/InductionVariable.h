#pragma once
#include "Symbol.h"
#include "ControlBlock.h"
#include "Cycle.h"
#include "Polyhedral.h"

namespace Cyclebite::Grammar
{
    class InductionVariable : public Symbol
    {
    public:
        InductionVariable( const std::shared_ptr<Cyclebite::Graph::DataValue>& n, const std::shared_ptr<Cycle>& c );
        const std::shared_ptr<Cyclebite::Graph::DataValue>& getNode() const;
        const std::shared_ptr<Cycle>& getCycle() const;
        StridePattern getPattern() const;
        const PolySpace getSpace() const;
        const std::set<std::shared_ptr<Cyclebite::Graph::ControlBlock>, Cyclebite::Graph::p_GNCompare>& getBody() const;
        bool isOffset(const llvm::Value* v) const;
        std::string dump() const override;
    private:
        std::shared_ptr<Cycle> cycle;
        std::shared_ptr<Cyclebite::Graph::DataValue> node;
        StridePattern pat;
        PolySpace space;
        /// Represents the blocks that this IV "controls", which basically means the loop body
        std::set<std::shared_ptr<Cyclebite::Graph::ControlBlock>, Cyclebite::Graph::p_GNCompare> body; 
    };
} // namespace Cyclebite::Grammar