#include "UniqueID.h"

using namespace Cyclebite::Profile::Backend::Memory;

UniqueID::UniqueID() : IID(getNextIID()) {}

UniqueID::~UniqueID() = default;

uint64_t UniqueID::getNextIID()
{
    return nextIID++;
}

void UniqueID::setNextIID(uint64_t next)
{
    if (next > nextIID)
    {
        nextIID = next + 1;
    }
}

uint64_t UniqueID::nextIID = 0;