//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include <memory>
#include <set>
#include "Polyhedral.h"
#include "Cycle.h"
#include "Graph/inc/DataValue.h"
#include "Graph/inc/ControlBlock.h"

namespace Cyclebite::Grammar
{
    class Dimension
    {
    public:
        Dimension() = delete;
        virtual ~Dimension();
        const std::shared_ptr<Cyclebite::Graph::DataValue>& getNode() const;
        const std::shared_ptr<Cycle>& getCycle() const;
        bool isOffset(const llvm::Value* v) const;
    protected:
        Dimension( const std::shared_ptr<Cyclebite::Graph::DataValue>& n, const std::shared_ptr<Cycle>& c );
        std::shared_ptr<Cycle> cycle;
        std::shared_ptr<Cyclebite::Graph::DataValue> node;
    };
} // namespace Cyclebite::Grammar