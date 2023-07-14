#pragma once
#include "Symbol.h"
#include "InductionVariable.h"

namespace TraceAtlas::Grammar
{
    class ReductionVariable : public Symbol
    {
    public:
        ReductionVariable(const std::shared_ptr<InductionVariable>& iv, const std::shared_ptr<TraceAtlas::Graph::DataNode>& n);
        std::string dump() const override;
        TraceAtlas::Graph::Operation getOp() const;
        const std::shared_ptr<TraceAtlas::Graph::DataNode>& getNode() const;
    private:
        std::shared_ptr<InductionVariable> iv;
        std::shared_ptr<TraceAtlas::Graph::DataNode> node;
        TraceAtlas::Graph::Operation bin;
    };
} // namespace TraceAtlas::Grammar