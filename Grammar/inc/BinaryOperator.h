#pragma once
#include "Expression.h"

namespace Cyclebite::Grammar
{
    class BinaryOperator : public Expression
    {
    public:
        BinaryOperator(const std::vector<std::shared_ptr<Symbol>>& vec, const Cyclebite::Graph::Operation& o);
        std::string dump() const override;
        
    private:
        Cyclebite::Graph::Operation op;
    };
} // namespace Cyclebite::Grammar