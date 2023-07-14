#pragma once
#include "Symbol.h"
#include <llvm/IR/Function.h>

namespace TraceAtlas::Grammar
{
    class ConstantFunction : public Symbol
    {
    public:
        ConstantFunction(const llvm::Function* f) : Symbol(std::string(f->getName())+"()"), f(f) {}
        std::string dump() const override;
        const llvm::Function* getFunction() const;
    private:
        const llvm::Function* f;
    };
} // namespace TraceAtlas::Grammar