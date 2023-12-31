//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "NonKernel.h"

using namespace std;
using namespace Cyclebite::Profile::Backend::Memory;

NonKernel::NonKernel(pair<int64_t, int64_t> ent) : CodeSection(ent) 
{
    blocks.insert(ent.second);
    contextLevel = 0;
}