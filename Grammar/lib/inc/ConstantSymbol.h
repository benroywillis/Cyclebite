//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include "Symbol.h"
#include "Util/Exceptions.h"

namespace Cyclebite::Grammar
{
    enum class ConstantType
    {
        SHORT,
        INT,
        FLOAT,
        DOUBLE,
        INT64,
        UNKNOWN
    };
    extern std::map<ConstantType, std::string> TypeToString;
    void initTypeToString();

    class ConstantSymbol : public Symbol
    {
    public:
        ConstantSymbol( const void* a, ConstantType type ) : Symbol("const")
        {
            initTypeToString();
            t = type;
            // interpret the void* input
            switch(t)
            {
                case ConstantType::SHORT : bits.s = *(short*)a; break;
                case ConstantType::INT   : bits.i = *(int*)a; break;
                case ConstantType::FLOAT : bits.f = *(float*)a; break;
                case ConstantType::DOUBLE: bits.d = *(double*)a; break;
                case ConstantType::INT64 : bits.l = *(long*)a; break;
                default: throw CyclebiteException("Cannot yet support "+TypeToString.at(t)+" in a ConstantSymbol!");
            }
        }
        std::string dump() const;
        std::string dumpHalide( const std::map<std::shared_ptr<Dimension>, std::shared_ptr<ReductionVariable>>& dimToRV ) const;
        virtual std::string dumpC() const;
        /// @brief Returns the value held in this constant
        /// @param ret A pointer to a place in memory to store the value (the allocation of this pointer must be at least 8 bytes)
        /// @return The type held by the constant. Interpret the value placed into the pointer with this type.
        ConstantType getVal(void* ret) const;
    protected:
        union {
            short s;
            int i;
            float f;
            double d;
            int64_t l;
        } bits;
        ConstantType t;
    };
} // namespace Cyclebite::Grammar