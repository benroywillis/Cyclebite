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
    std::set<std::shared_ptr<InductionVariable>> getInductionVariables(const std::shared_ptr<Task>& t);
    std::set<std::shared_ptr<ReductionVariable>> getReductionVariables(const std::shared_ptr<Task>& t, const std::set<std::shared_ptr<InductionVariable>>& vars);
    std::set<std::shared_ptr<BasePointer>> getBasePointers(const std::shared_ptr<Task>& t);
    std::set<std::shared_ptr<Collection>> getCollections( const std::set<std::shared_ptr<IndexVariable>>& IVs );
    void Process(const std::set<std::shared_ptr<Task>>& tasks);
} // namespace Cyclebite::Grammar