#pragma once
#include "Expression.h"
#include "ReductionVariable.h"

namespace TraceAtlas::Grammar
{
    class Reduction : public Expression
    {
    public:
        Reduction(const std::shared_ptr<ReductionVariable>& var, const std::vector<std::shared_ptr<Symbol>>& s, const std::vector<TraceAtlas::Graph::Operation>& o );
        ~Reduction() = default;
        std::string dump() const override;
    private:
        std::shared_ptr<ReductionVariable> var;
    };
} // namespace TraceAtlas::Grammar