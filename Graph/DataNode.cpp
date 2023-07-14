#include "DataNode.h"

using namespace TraceAtlas::Graph;

std::map<Operation, const char *> TraceAtlas::Graph::OperationToString;

Operation TraceAtlas::Graph::GetOp(unsigned int op)
{
    // this function maps an LLVM op into the Operation scoped enum
    // these opcodes are subject to change, at the time of this functions writing the llvm version was 13
    switch (op)
    {
        // terminators
        case 1:
            return Operation::ret;
        case 2:
            return Operation::br;
        case 3:
            return Operation::sw;
        case 4:
            return Operation::ibr;
        case 5:
            return Operation::invoke;
        case 6:
            return Operation::resume;
        // the next 5 (7-11) are terminator instructions that we don't care about yet
        // binary arithmetic
        case 12:
            return Operation::fneg;
        case 13:
            return Operation::add;
        case 14:
            return Operation::fadd;
        case 15:
            return Operation::sub;
        case 16:
            return Operation::fsub;
        case 17:
            return Operation::mul;
        case 18:
            return Operation::fmul;
        case 19:
            return Operation::udiv;
        case 20:
            return Operation::sdiv;
        case 21:
            return Operation::fdiv;
        case 22:
            return Operation::urem;
        case 23:
            return Operation::srem;
        case 24:
            return Operation::frem;
        case 25:
            return Operation::sl;
        case 26:
            return Operation::sr;
        case 27:
            return Operation::asr;
        case 28:
            return Operation::andop;
        case 29:
            return Operation::orop;
        case 30:
            return Operation::xorop;
        // memory ops
        case 31:
            return Operation::stackpush;
        case 32:
            return Operation::load;
        case 33:
            return Operation::store;
        case 34:
            return Operation::gep;
        case 37:
            return Operation::atomicrmw;
        // casting
        case 38:
            return Operation::trunc;
        case 39:
            return Operation::zext;
        case 40:
            return Operation::sext;
        case 41:
            return Operation::fptoui;
        case 42:
            return Operation::fptosi;
        case 43:
            return Operation::uitofp;
        case 44:
            return Operation::sitofp;
        case 45:
            return Operation::fptrunc;
        case 46:
            return Operation::fpext;
        case 47:
            return Operation::ptrtoint;
        case 48:
            return Operation::inttoptr;
        case 49:
            return Operation::bitcast;
        case 50:
            return Operation::addrspacecast;
        // comparators
        case 53:
            return Operation::icmp;
        case 54:
            return Operation::fcmp;
        case 55:
            return Operation::phi;
        case 56:
            return Operation::call;
        case 57:
            return Operation::select;
        // vector ops
        case 61:
            return Operation::extractelem;
        case 62:
            return Operation::insertelem;
        case 63:
            return Operation::shufflevec;
        case 64:
            return Operation::extractvalue;
        // other stuff
        case 66:
            return Operation::landingpad;
        case 67:
            return Operation::freeze;
        // everything else we don't care about for now
        default:
            return Operation::nop;
    }
}

DataNode::DataNode(const llvm::Instruction* inst, DNC t) : GraphNode(), inst(inst), type(t)
{
    initOpToString();
}

const llvm::Instruction* DataNode::getInst() const
{
    return inst;
}

bool DataNode::isTerminator() const
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

bool DataNode::isCaller() const
{
    if( op == Operation::call )
    {
        return true;
    }
    return false;
}

bool DataNode::isState() const
{
    return type == DNC::State;
}

bool DataNode::isMemory() const
{
    return type == DNC::Memory;
}

bool DataNode::isFunction() const
{
    return type == DNC::Function;
}

void DataNode::initOpToString()
{
    map_init(OperationToString)
        // terminator ops
        (Operation::ret, "function_return")(Operation::br, "br")(Operation::sw, "switch")(Operation::ibr, "indirect_br")(Operation::invoke, "invoke")(Operation::resume, "resume")
        // memory ops
        (Operation::load, "ld")(Operation::store, "st")(Operation::stackpush, "stack")(Operation::gep, "gep")(Operation::atomicrmw, "atomicrmw")
        // binary arithmetic
        (Operation::fneg, "fneg")(Operation::mul, "x")(Operation::fmul, "fx")(Operation::udiv, "u/")(Operation::sdiv, "s/")(Operation::fdiv, "f/")(Operation::urem, "u%")(Operation::srem, "s%")(Operation::frem, "f%")(Operation::add, "+")(Operation::fadd, "f+")(Operation::sub, "-")(Operation::fsub, "f-")(Operation::gt, ">")(Operation::gte, ">=")(Operation::lt, "<")(Operation::lte, "<=")(Operation::sr, ">>")(Operation::asr, ">>>")(Operation::sl, "<<")(Operation::andop, "&&")(Operation::orop, "||")(Operation::xorop, "XOR")
        // casting
        (Operation::trunc, "trunc")(Operation::sext, "sext")(Operation::zext, "zext")(Operation::fptoui, "fptoui")(Operation::fptosi, "fptosi")(Operation::uitofp, "uitofp")(Operation::sitofp, "sitofp")(Operation::fptrunc, "fptrunc")(Operation::fpext, "fpext")(Operation::ptrtoint, "ptrtoint")(Operation::inttoptr, "inttoptr")(Operation::bitcast, "bitcast")(Operation::addrspacecast, "addrspacecast")
        // comparators
        (Operation::icmp, "icmp")(Operation::fcmp, "fcmp")(Operation::phi, "phi")(Operation::call, "call")(Operation::select, "select")
        // vector ops
        (Operation::extractelem, "extractelem")(Operation::insertelem, "insertelem")(Operation::shufflevec, "shufflevec")(Operation::extractvalue, "extractvalue")
        // other stuff
        (Operation::landingpad, "landingpad")(Operation::freeze, "freeze")
        // default case
        (Operation::nop, "nop");
}