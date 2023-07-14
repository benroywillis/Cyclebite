#include "CodeInstance.h"

using namespace std;
using namespace Cyclebite::Profile::Backend::Memory;

CodeInstance::CodeInstance() : UniqueID() {}

const Iteration& CodeInstance::getMemory() const 
{
    return memoryData;
}

void CodeInstance::addIteration(const shared_ptr<Iteration>& newIteration)
{
    for( const auto& wt : newIteration->wTuples )
    {
        merge_tuple_set(memoryData.wTuples, wt);
    }
    for( const auto& rt : newIteration->rTuples )
    {
        merge_tuple_set(memoryData.rTuples, rt);
    }
}

void CodeInstance::addIteration(const Iteration& newIteration)
{
    for( const auto& wt : newIteration.wTuples )
    {
        merge_tuple_set(memoryData.wTuples, wt);
    }
    for( const auto& rt : newIteration.rTuples )
    {
        merge_tuple_set(memoryData.rTuples, rt);
    }
}