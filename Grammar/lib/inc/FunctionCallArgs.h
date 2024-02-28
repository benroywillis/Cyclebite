//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include <cstdint>
#include <vector>
#include <llvm/IR/Instructions.h>

namespace Cyclebite::Grammar
{
    class FunctionCallArgs
    {
    public:
        enum class T_member
        {
            UINT8_T,
            INT8_T,
            UINT16_T,
            INT16_T,
            UINT32_T,
            INT32_T,
            UINT64_T,
            INT64_T,
            SHORT,
            FLOAT,
            DOUBLE,
            VOID
        };

        union member {
            uint8_t a;
            int8_t b;
            uint16_t c;
            int16_t d;
            uint32_t e;
            int32_t f;
            uint64_t g;
            int64_t h;
            short i;
            float j;
            double k;
            void* l;
        };

        FunctionCallArgs();
        FunctionCallArgs( const llvm::CallBase* con );
        ~FunctionCallArgs() = default;
        std::vector<member> args;
        std::vector<T_member> types;
        void* getMember(unsigned i) const;
    };
} // namespace Cyclebite::Grammar