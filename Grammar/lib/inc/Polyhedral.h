//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
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
    
    enum class STATIC_VALUE 
    {
        INVALID = INT_MIN,
        UNDETERMINED = INT_MIN+1
    };

    struct PolySpace
    {
        int min;
        int max;
        int stride;
        StridePattern pattern;
        PolySpace()
        {
            min     = static_cast<int>(STATIC_VALUE::INVALID);
            max     = static_cast<int>(STATIC_VALUE::INVALID);
            stride  = static_cast<int>(STATIC_VALUE::INVALID);
            pattern = StridePattern::Random;
        }
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