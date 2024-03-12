//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include "Symbol.h"
#include "Util/Exceptions.h"
#include <llvm/IR/Constant.h>

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
        /// @param c    Pointer to the llvm::Constant - this must be pre-allocated
        /// @param a    Pointer to the value of the constant - this does not have to be pre-allocated, it's a pointer so it can be passed as any type
        /// @param type Tells the constructor how to interpret param "a" - "a" is casted using this type and put into the appropriate member of a union
        ConstantSymbol( const llvm::Constant* c, const void* a, ConstantType type ) : Symbol("const"), c(c)
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
        const llvm::Constant* getConstant() const;
        std::string dump() const;
        std::string dumpHalide( const std::map<std::shared_ptr<Symbol>, std::shared_ptr<Symbol>>& symbol2Symbol ) const;
        virtual std::string dumpC() const;
        /// @brief Returns the value held in this constant
        /// @param ret A pointer to a place in memory to store the value (the allocation of this pointer must be at least 8 bytes)
        /// @return The type held by the constant. Interpret the value placed into the pointer with this type.
        ConstantType getVal(void* ret) const;
    protected:
        const llvm::Constant* c;
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