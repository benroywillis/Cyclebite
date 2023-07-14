#include "CodeSection.h"

using namespace std;
using namespace Cyclebite::Profile::Backend::Memory;

CodeSection::CodeSection(set<int64_t> b, map<int64_t, set<int64_t>> ent, map<int64_t, set<int64_t>> ex) : UniqueID(), blocks(b), entrances(ent), exits(ex) {}

CodeSection::CodeSection(pair<int64_t, int64_t> entranceEdge) : UniqueID()
{
    entrances[entranceEdge.first].insert(entranceEdge.second);
    blocks.insert(entranceEdge.second);
}

const vector<shared_ptr<Epoch>>& CodeSection::getInstances() const
{
    return instances;
}

const shared_ptr<Epoch>& CodeSection::getInstance(unsigned int i) const
{
    return instances[i];
}

const shared_ptr<Epoch>& CodeSection::getCurrentInstance() const
{
    return instances.back();
}

void CodeSection::addInstance(shared_ptr<Epoch> newInstance)
{
    instances.push_back(newInstance);
}