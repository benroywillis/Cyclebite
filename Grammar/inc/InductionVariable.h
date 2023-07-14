#pragma once
#include "Symbol.h"
#include "ControlBlock.h"
#include "Cycle.h"

namespace TraceAtlas::Grammar
{
    /// @brief Defines stride patterns
    enum class StridePattern
    {
        Sequential,
        Random
    };

    struct PolySpace
    {
        uint32_t min;
        uint32_t max;
        uint32_t stride;
    };
    
    class InductionVariable : public Symbol
    {
    public:
        InductionVariable( const std::shared_ptr<TraceAtlas::Graph::DataNode>& n, const std::shared_ptr<Cycle>& c );
        const std::shared_ptr<TraceAtlas::Graph::DataNode>& getNode() const;
        const std::shared_ptr<Cycle>& getCycle() const;
        StridePattern getPattern() const;
        const PolySpace getSpace() const;
        const std::set<std::shared_ptr<TraceAtlas::Graph::ControlBlock>, TraceAtlas::Graph::p_GNCompare>& getBody() const;
        bool isOffset(const llvm::Value* v) const;
        std::string dump() const override;
    private:
        std::shared_ptr<Cycle> cycle;
        std::shared_ptr<TraceAtlas::Graph::DataNode> node;
        StridePattern pat;
        PolySpace space;
        /// Represents the blocks that this IV "controls", which basically means the loop body
        std::set<std::shared_ptr<TraceAtlas::Graph::ControlBlock>, TraceAtlas::Graph::p_GNCompare> body; 
    };
} // namespace TraceAtlas::Grammar