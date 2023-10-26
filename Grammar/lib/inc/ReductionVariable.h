//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include "Symbol.h"
#include "InductionVariable.h"
#include "Graph/inc/Inst.h"

namespace Cyclebite::Grammar
{
    class Task;
    class ReductionVariable : public Symbol
    {
    public:
        ReductionVariable(const std::shared_ptr<InductionVariable>& iv, const std::shared_ptr<Cyclebite::Graph::DataValue>& n, const std::shared_ptr<Graph::DataValue>& addr);
        std::string dump() const override;
        Cyclebite::Graph::Operation getOp() const;
        /// @brief The node that holds the accumulation
        ///
        /// e.g., in a mac, this is the add
        const std::shared_ptr<Cyclebite::Graph::DataValue>& getNode() const;
        /// Returns the place that this reduction is stored to
        ///
        /// In the case of a looped-phi, this is the phi node
        /// IN the case of a heaped reduction, this is the pointer the reduction is stored to
        const std::shared_ptr<Graph::DataValue>& getAddress() const;
    private:
        /// @brief The induction variable that facilitates this reduction
        std::shared_ptr<InductionVariable> iv;
        /// @brief The node that carries out the reduction.
        /// 
        /// In the case that this is a looped-phi reduction, the node is the binary operator that stores the reduction into the reduction variable
        /// In the case this this is a heaped reduction, the node is the binary operator that is stored to the heap address
        std::shared_ptr<Cyclebite::Graph::DataValue> node;
        /// The operation of the reduction (e.g., in multiply-accumulate this is "add")
        Cyclebite::Graph::Operation bin;
        /// The place this reduction variable is stored to
        /// In the case of looped-phi, this is the phi node
        /// In the case of heaped reduction, this is the pointer the reduction is stored to
        std::shared_ptr<Graph::DataValue> address;
    };
    std::set<std::shared_ptr<ReductionVariable>> getReductionVariables(const std::shared_ptr<Task>& t, const std::set<std::shared_ptr<InductionVariable>>& vars);
} // namespace Cyclebite::Grammar