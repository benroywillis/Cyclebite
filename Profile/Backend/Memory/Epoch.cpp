//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "Epoch.h"

using namespace Cyclebite::Profile::Backend::Memory;

Epoch::Epoch() : UniqueID(), kernel(nullptr) {}

void Epoch::updateBlocks(int64_t id)
{
    freq[id] += 1;
    blocks.insert(id);
}

uint64_t Epoch::getMaxFreq()
{
    uint64_t maxFreq = 0;
    for( const auto& f : freq )
    {
        if( f.second > maxFreq )
        {
            maxFreq = f.second;
        }
    }
    return maxFreq;
}