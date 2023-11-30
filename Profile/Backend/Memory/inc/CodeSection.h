//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include "UniqueID.h"
#include <map>
#include <set>
#include <vector>

namespace Cyclebite::Profile::Backend::Memory
{
    class Epoch;
    class CodeSection : public UniqueID
    {
    public:
        std::set<int64_t> blocks;
        std::map<int64_t, std::set<int64_t>> entrances;
        std::map<int64_t, std::set<int64_t>> exits;
        // this is not set at construction time, it must be set after all kernels are read
        int contextLevel = -1;
        CodeSection(std::set<int64_t> b, std::map<int64_t, std::set<int64_t>> ent, std::map<int64_t, std::set<int64_t>> ex);
        CodeSection(std::pair<int64_t, int64_t> entranceEdge);
        ~CodeSection() = default;
        const std::vector<std::shared_ptr<Epoch>>& getInstances() const;
        const std::shared_ptr<Epoch>& getInstance(unsigned int i) const;
        const std::shared_ptr<Epoch>& getCurrentInstance() const;
        void addInstance(std::shared_ptr<Epoch> newInstance);

    protected:
        std::vector<std::shared_ptr<Epoch>> instances;
    };

    // sorts contextLevels from least to greatest, parent to child
    struct HierarchySort
    {
        bool operator()(const std::shared_ptr<CodeSection>& lhs, const std::shared_ptr<CodeSection>& rhs) const
        {
            if (lhs->contextLevel == rhs->contextLevel)
            {
                return lhs->IID < rhs->IID;
            }
            return lhs->contextLevel < rhs->contextLevel;
        }
    };
} // namespace Cyclebite::Profile::Backend::Memory