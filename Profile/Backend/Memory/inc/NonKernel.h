#pragma once
#include "CodeSection.h"

namespace TraceAtlas::Profile::Backend::Memory
{
    class NonKernel : public CodeSection
    {
    public:
        NonKernel(std::pair<int64_t, int64_t> ent);
    };
} // namespace TraceAtlas::Profile::Backend