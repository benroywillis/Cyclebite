//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "ConstantSymbol.h"
#include "Graph/inc/Operation.h"

using namespace Cyclebite::Grammar;
using namespace std;

std::map<ConstantType, string> Cyclebite::Grammar::TypeToString;

void Cyclebite::Grammar::initTypeToString()
{
    Cyclebite::Graph::map_init(TypeToString) (ConstantType::SHORT, "short")(ConstantType::INT, "int")(ConstantType::FLOAT, "float")(ConstantType::DOUBLE, "double")(ConstantType::UNKNOWN, "");
}

const llvm::Constant* ConstantSymbol::getConstant() const
{
    return c;
}

string ConstantSymbol::dump() const
{
    switch(t)
    {
        case ConstantType::SHORT : return to_string(bits.s);
        case ConstantType::INT   : return to_string(bits.i);
        case ConstantType::FLOAT : return to_string(bits.f);
        case ConstantType::DOUBLE: return to_string(bits.d);
        case ConstantType::INT64 : return to_string(bits.l);
        default                  : return "0";
    }
}

string ConstantSymbol::dumpHalide( const map<shared_ptr<Dimension>, shared_ptr<ReductionVariable>>& dimToRV ) const
{
    return dump();
}

string ConstantSymbol::dumpC() const
{
    switch(t)
    {
        case ConstantType::SHORT  : return "short "+name+" = "+dump(); break;
        case ConstantType::INT    : return "int "+name+" = "+dump(); break;
        case ConstantType::FLOAT  : return "float "+name+" = "+dump(); break;
        case ConstantType::DOUBLE : return "double "+name+" = "+dump(); break;
        case ConstantType::INT64  : return "long "+name+" = "+dump(); break;
        default                   : return "";
    }
}

ConstantType ConstantSymbol::getVal(void* ret) const
{
    switch(t)
    {
        case ConstantType::SHORT:
        {
            *(short*)ret = bits.s;
            return ConstantType::SHORT;
        }
        case ConstantType::INT:
        {
            *(int*)ret = bits.i;
            return ConstantType::INT;
        }
        case ConstantType::FLOAT:
        {
            *(float*)ret = bits.f;
            return ConstantType::FLOAT;
        }
        case ConstantType::DOUBLE:
        {
            *(double*)ret = bits.d;
            return ConstantType::DOUBLE;
        }
        default:
        {
            return ConstantType::UNKNOWN;
        }
    }
}