#pragma once
#include "Symbol.h"

namespace Cyclebite::Grammar
{
    enum class ConstantType
    {
        fp64,
        fp32,
        fp16,
        int64_t,
        uint64_t,
        int32_t,
        uint32_t,
        int16_t,
        uint16_t,
        int8_t,
        uint8_t,
        int1_t
    };

    class ConstantSymbol : public Symbol
    {
    public:
        ConstantSymbol(int64_t b) : Symbol("const") , bits(b) {}
        std::string dump() const;
    private:
        int64_t bits;
    };
} // namespace Cyclebite::Grammar