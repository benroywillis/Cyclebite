#pragma once
#include "OperatorExpression.h"
#include <llvm/IR/Function.h>

namespace Cyclebite::Grammar
{
    class FunctionExpression : public OperatorExpression
    {
    public:
        FunctionExpression(const llvm::Function* f, const std::vector<std::shared_ptr<Symbol>>& args) : OperatorExpression( Cyclebite::Graph::Operation::call, args), f(f) {}
        std::string dump() const override;
        const llvm::Function* getFunction() const;
    private:
        const llvm::Function* f;
    };
} // namespace Cyclebite::Grammar