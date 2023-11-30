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
        ReductionVariable( const std::set<std::shared_ptr<Dimension>, DimensionSort>& dims, 
                           const std::vector<std::shared_ptr<Cyclebite::Graph::DataValue>>& addrs, 
                           const std::shared_ptr<Cyclebite::Graph::DataValue>& n);
        std::string dump() const override;
        Cyclebite::Graph::Operation getOp() const;
        /// @brief The node that holds the accumulation
        ///
        /// e.g., in a mac, this is the add
        const std::shared_ptr<Cyclebite::Graph::DataValue>& getNode() const;
        /// @brief Returns all Cyclebite::Grammar::Dimensions that carry out this reduction, in hierarchical order from parent-most to child-most.
        const std::set<std::shared_ptr<Dimension>, DimensionSort>& getDimensions() const;
        /// @brief Returns the place(s) this reduction is stored to
        ///
        /// In the case of a looped-phi, this is the phi node
        /// - in the case of a multi-dimensional phi, these are the phi nodes that store the multi-dimensional reduction in hierarchical order from parent-most to child-most
        /// In the case of a heaped reduction, this is the pointer the reduction is stored to
        const std::vector<std::shared_ptr<Graph::DataValue>>& getAddresses() const;
    private:
        /// @brief The dimensions that facilitate this reduction
        std::set<std::shared_ptr<Dimension>, DimensionSort> dimensions;
        /// @brief The addresses are the phis that store the result of the reduction
        ///
        /// They work alongside the phis in "dimensions"
        /// These should be sorted in hierarchical order, parent-most to child-most
        std::vector<std::shared_ptr<Graph::DataValue>> addresses;
        /// @brief The node that carries out the reduction.
        /// 
        /// In the case that this is a looped-phi reduction, the node is the binary operator that stores the reduction into the reduction variable
        /// In the case this this is a heaped reduction, the node is the binary operator that is stored to the heap address
        std::shared_ptr<Cyclebite::Graph::DataValue> node;
        /// The operation of the reduction (e.g., in multiply-accumulate this is "add")
        Cyclebite::Graph::Operation bin;
    };
    std::set<std::shared_ptr<ReductionVariable>> getReductionVariables(const std::shared_ptr<Task>& t, const std::set<std::shared_ptr<InductionVariable>>& vars);
} // namespace Cyclebite::Grammar