#include "Kernel.h"
#include "Epoch.h"

using namespace TraceAtlas::Profile::Backend::Memory;
using namespace std;

Kernel::Kernel(set<int64_t> b, map<int64_t, set<int64_t>> ent, map<int64_t, set<int64_t>> ex) : CodeSection(b, ent, ex) {}

Kernel::Kernel(set<int64_t> b, map<int64_t, set<int64_t>> ent, map<int64_t, set<int64_t>> ex, int id) : CodeSection(b, ent, ex), kid(id) {}

const shared_ptr<Epoch> Kernel::getKI(unsigned int i) const
{
    return static_pointer_cast<Epoch>(instances[i]);
}

const shared_ptr<Epoch> Kernel::getCurrentKI() const
{
    if (instances.empty())
    {
        return nullptr;
    }
    return static_pointer_cast<Epoch>(instances.back());
}

const vector<shared_ptr<Epoch>> Kernel::getKIs() const
{
    vector<shared_ptr<Epoch>> KernelInstances;
    for (const auto &i : instances)
    {
        KernelInstances.push_back(static_pointer_cast<Epoch>(i));
    }
    return KernelInstances;
}

void Kernel::addInstance(shared_ptr<Epoch> newInstance)
{
    instances.push_back(newInstance);
}