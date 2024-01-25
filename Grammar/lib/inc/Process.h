//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include "DataGraph.h"
#include "ReductionVariable.h"
#include "Collection.h"

namespace Cyclebite::Grammar
{
    class Task;
    struct TaskIDCompare;
    void Process(const std::set<std::shared_ptr<Task>, TaskIDCompare>& tasks);
} // namespace Cyclebite::Grammar