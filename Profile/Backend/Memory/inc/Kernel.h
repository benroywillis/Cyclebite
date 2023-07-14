#pragma once
#include "CodeSection.h"
#include <string>

namespace TraceAtlas::Profile::Backend::Memory
{
    class Epoch;
    class Kernel : public CodeSection
    {
    public:
        int kid;
        std::string label;
        std::set<std::shared_ptr<Kernel>> parents;
        std::set<std::shared_ptr<Kernel>> children;
        Kernel(std::set<int64_t> b, std::map<int64_t, std::set<int64_t>> ent, std::map<int64_t, std::set<int64_t>> ex);
        Kernel(std::set<int64_t> b, std::map<int64_t, std::set<int64_t>> ent, std::map<int64_t, std::set<int64_t>> ex, int id);
        const std::shared_ptr<Epoch> getKI(unsigned int i) const;
        const std::shared_ptr<Epoch> getCurrentKI() const;
        const std::vector<std::shared_ptr<Epoch>> getKIs() const;
        void addInstance(std::shared_ptr<Epoch> newInstance);
    };
} // namespace TraceAtlas::Profile::Backend