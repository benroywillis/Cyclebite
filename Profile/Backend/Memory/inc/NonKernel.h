//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include "CodeSection.h"

namespace Cyclebite::Profile::Backend::Memory
{
    class NonKernel : public CodeSection
    {
    public:
        NonKernel(std::pair<int64_t, int64_t> ent);
    };
} // namespace Cyclebite::Profile::Backend