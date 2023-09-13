#pragma once
#include "DataValue.h"
#include "Operation.h"
#include <llvm/IR/Instruction.h>
#include <map>

namespace Cyclebite::Graph
{
    // Data Node Category
    enum class DNC
    {
        None,
        State,
        Function,
        Memory
    };

    class Inst : public DataValue
    {
    public:
        std::shared_ptr<class ControlBlock> parent;
        Inst(const llvm::Instruction* inst, DNC t = DNC::None);
        const llvm::Instruction* getInst() const;
        Operation getOp() const; 
        bool isState() const;
        bool isFunction() const;
        bool isMemory() const;
        bool isFunctionCall() const;
        bool isTerminator() const;
        bool isCaller() const;
        bool isBinaryOp() const;
        bool isCastOp() const;
        bool isComparator() const;
    private:
        const llvm::Instruction* inst;
        DNC type;
        Operation op;
    };
} // namespace Cyclebite::Graph