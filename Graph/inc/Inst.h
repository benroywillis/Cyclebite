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
        Operation op;
        std::shared_ptr<class ControlBlock> parent;
        Inst(const llvm::Instruction* inst, DNC t = DNC::None);
        const llvm::Instruction* getInst() const; 
        bool isState() const;
        bool isFunction() const;
        bool isMemory() const;
        bool isTerminator() const;
        bool isCaller() const;
    private:
        const llvm::Instruction* inst;
        DNC type;
    };
} // namespace Cyclebite::Graph