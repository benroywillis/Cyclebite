#pragma once
#include "Expression.h"

namespace TraceAtlas::Grammar
{
    class BinaryOperator : public Expression
    {
    public:
        BinaryOperator(const std::vector<std::shared_ptr<Symbol>>& vec, const TraceAtlas::Graph::Operation& o);
        std::string dump() const override;
        
    private:
        TraceAtlas::Graph::Operation op;
    };
} // namespace TraceAtlas::Grammar