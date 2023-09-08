#include "Inst.h"

using namespace Cyclebite::Graph;


Inst::Inst(const llvm::Instruction* inst, DNC t) : DataValue(inst), inst(inst), type(t)
{
    initOpToString();
}

const llvm::Instruction* Inst::getInst() const
{
    return inst;
}

bool Inst::isTerminator() const
{
    if ((op == Operation::ret) || (op == Operation::br) || (op == Operation::sw) || (op == Operation::ibr) || (op == Operation::invoke) || (op == Operation::resume))
    {
        return true;
    }
    else
    {
        return false;
    }
}

bool Inst::isCaller() const
{
    if( op == Operation::call )
    {
        return true;
    }
    return false;
}

bool Inst::isState() const
{
    return type == DNC::State;
}

bool Inst::isMemory() const
{
    return type == DNC::Memory;
}

bool Inst::isFunction() const
{
    return type == DNC::Function;
}