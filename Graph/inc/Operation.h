//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include <map>

namespace Cyclebite::Graph
{
    // all this stuff pertains to the llvm api and the operations we care about
    // LLVM op information can be found at https://github.com/llvm/llvm-project/blob/main/llvm/include/llvm/IR/Instruction.def

    // this stuff is just fancy modern c++ to map each member of the Operation enum to a string that represents it (most useful for exporting graphs of dataflow)
    template <typename T>
    struct map_init_helper
    {
        T &data;
        map_init_helper(T &d) : data(d) {}
        map_init_helper &operator()(typename T::key_type const &key, typename T::mapped_type const &value)
        {
            data[key] = value;
            return *this;
        }
    };

    template <typename T>
    map_init_helper<T> map_init(T &item)
    {
        return map_init_helper<T>(item);
    }

    enum class Operation
    {
        // terminators;
        ret,
        br,
        sw,
        ibr,
        invoke,
        resume,
        // memory
        stackpush,
        load,
        store,
        gep,
        atomicrmw,
        // binary arith
        fneg,
        mul,
        fmul,
        udiv,
        sdiv,
        fdiv,
        urem,
        srem,
        frem,
        add,
        fadd,
        sub,
        fsub,
        gt,
        gte,
        lt,
        lte,
        sr,
        asr,
        sl,
        andop,
        orop,
        xorop,
        // casting
        trunc,
        zext,
        sext,
        fptoui,
        fptosi,
        uitofp,
        sitofp,
        fptrunc,
        fpext,
        ptrtoint,
        inttoptr,
        bitcast,
        addrspacecast,
        // comparators
        icmp,
        fcmp,
        phi,
        call,
        select,
        // vector ops and atomic
        extractelem,
        insertelem,
        extractvalue,
        shufflevec,
        // other stuff
        landingpad,
        freeze,
        // default case
        nop
    };
    void initOpToString();
    extern std::map<Operation, const char *> OperationToString;
    /// @brief Maps an LLVM instruction to its Cyclebite::Graph::Operation
    /// @param op Opcode from the llvm Instruction (e.g., inst->getOpcode())
    /// @return Cyclebite Operation enum member corresponding to the input arg
    Operation GetOp(unsigned int op);
    bool isTerminator(Operation op);
    bool isMemoryInst(Operation op);
    bool isBinaryOp(Operation op);
    bool isCastOp(Operation op);
    bool isComparator(Operation op);
    bool isVectorOp(Operation op);
} // namespace Cyclebite:Graph