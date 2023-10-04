//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "Inst.h"

using namespace Cyclebite::Graph;


Inst::Inst(const llvm::Instruction* inst, DNC t) : DataValue(inst), inst(inst), type(t)
{
    op = GetOp(inst->getOpcode());
    initOpToString();
}

const llvm::Instruction* Inst::getInst() const
{
    return inst;
}

Operation Inst::getOp() const
{
    return op;
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

bool Inst::isFunctionCall() const
{
    return op == Operation::call || op == Operation::invoke;
}

bool Inst::isBinaryOp() const
{
    if( op == Operation::add   ||
        op == Operation::fadd  ||
        op == Operation::sub   ||
        op == Operation::fsub  ||
        op == Operation::mul   ||
        op == Operation::fmul  ||
        op == Operation::fneg  ||
        op == Operation::fdiv  ||
        op == Operation::sdiv  ||
        op == Operation::udiv  ||
        op == Operation::urem  ||
        op == Operation::srem  ||
        op == Operation::frem  ||
        op == Operation::gt    ||
        op == Operation::gte   ||
        op == Operation::lt    ||
        op == Operation::lte   ||
        op == Operation::sl    ||
        op == Operation::sr    ||
        op == Operation::asr   ||
        op == Operation::andop ||
        op == Operation::orop  ||
        op == Operation::xorop )
    {
        return true;
    }
    return false;
}

bool Inst::isCastOp() const
{
    if( op == Operation::trunc ||
        op == Operation::zext ||
        op == Operation::sext ||
        op == Operation::fptoui ||
        op == Operation::fptosi ||
        op == Operation::uitofp ||
        op == Operation::sitofp ||
        op == Operation::fptrunc ||
        op == Operation::fpext ||
        op == Operation::ptrtoint ||
        op == Operation::inttoptr ||
        op == Operation::bitcast ||
        op == Operation::addrspacecast)
    {
        return true;
    }
    return false;
}

bool Inst::isComparator() const
{
    if( op == Operation::icmp ||
        op == Operation::fcmp ||
        op == Operation::phi  ||
        op == Operation::select )
    {
        return true;
    }
    return false;
}