#pragma once
#include <climits>
#include <cstdint>
#include "Graph/inc/Operation.h"

namespace Cyclebite::Grammar
{
    /// @brief Defines stride patterns
    enum class StridePattern
    {
        Sequential,
        Random
    };

    struct PolySpace
    {
        uint32_t min;
        uint32_t max;
        uint32_t stride;
        StridePattern pattern;
    };
    
    enum class IV_BOUNDARIES 
    {
        INVALID = INT_MIN,
        UNDETERMINED = INT_MIN+1
    };

    struct AffineOffset
    {
        int constant;
        Graph::Operation transform;
        AffineOffset()
        {
            constant = 0;
            transform = Graph::Operation::nop;
        }
    };
} // namespace Cyclebite::Grammar