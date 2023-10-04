// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
#include "Iteration.h"

using namespace Cyclebite::Profile::Backend::Memory;
using namespace std;

Iteration::Iteration() = default;

void Iteration::clear()
{
    time = 0;
    rTuples.clear();
    wTuples.clear();
}