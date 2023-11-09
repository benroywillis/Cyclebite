//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include "Collection.h"
#include "Graph/inc/Inst.h"

namespace Cyclebite::Grammar
{
    class Cycle;
    /// @brief Represents the rhs of a computation statement
    ///
    /// In the Halide example
    /// RDOM k(-2, 2, -2, 2)
    /// foo(y, x, c) += input(y+k, x+l, c) * kernel(k, l)
    /// "input(y+k, x+l, c) * kernel(k, l)" is an Cyclebite::Grammar::Expression
    /// "input(y+k, x+l, c)" and "kernel(k, l)" are Cyclebite::Grammar::Collections
    class Expression : public Symbol
    {
    public:
        /// @brief Standalone constructor. Useful for building expression classes.
        Expression( const std::shared_ptr<Task>& ta, const std::vector<std::shared_ptr<Symbol>>& in, const std::vector<Cyclebite::Graph::Operation>& o, const std::shared_ptr<Symbol>& out = nullptr, const std::string name = "expr" );
        ~Expression() = default;
        const std::shared_ptr<Task>& getTask() const;
        /// @brief Returns the input values that this expression requires to do its computation
        /// @return All input values to the expression including Collections, TaskParameters, TaskRegisters, and other Expressions 
        const std::vector<std::shared_ptr<Symbol>>& getSymbols() const;
        const std::vector<Cyclebite::Graph::Operation>& getOps() const;
        /// Returns the collections that input into the expression
        const std::set<std::shared_ptr<Collection>> getCollections() const;
        /// @brief Returns all memory-related inputs required to evaluate this expression
        ///
        /// Memory-related inputs come from two places: Collections (heap addresses) and TaskRegisters (phi nodes). 
        /// @return A list of Collections and TaskRegisters required to evaluate this Expression
        const std::set<std::shared_ptr<Symbol>>& getInputs() const;
        /// @brief Returns the place in memory that this expression is stored within, if any
        ///
        /// The two accepted answers here (so far) are a heap address (represented as a Cyclebite::Grammar::Collection) and a register (represented as a Cyclebite::Grammar::TaskRegister)
        /// Background: 
        /// - most expressions are not stored - they are intermediates in the processing of an atom. In this case, the expression is not stored, and this method will return nullptr
        /// - in some cases, the expression is stored in a memory address. Thus, it may be used in a future computation somewhere
        /// @retval Either a Collection or TaskRegister that this expression is stored to after its evaluation. If the expression is not stored, nullptr is returned.
        const std::shared_ptr<Symbol>& getOutput() const;
        std::string dump() const override;
    protected:
        /// the task in which the expression is derived
        std::shared_ptr<Task> t;
        // contains all collections referenced by this expression
        std::set<std::shared_ptr<Symbol>> inputs;
        std::shared_ptr<Symbol> output;
        // contains operators that lie in between each symbol entry. Will always be of size (symbols.size() - 1)
        std::vector<Cyclebite::Graph::Operation> ops;
        // contains the symbols, in op order, for the expression. Will always be of size (ops.size() + 1)
        std::vector<std::shared_ptr<Symbol>> symbols;
        static bool printedName;
        static void FindInputs( Expression* expr );
    };
    class ReductionVariable;
    /// @brief Expression builder for a function expression
    /// @param t The task in which this function group belongs
    /// @param insts A vector of all instructions in the function group, in the order they operate
    /// @param rv A reduction variable, if necessary. If this argument is non-null, the returned expression is a Reduction. Otherwise it is an Expression.
    /// @param colls The collections in the task
    /// @return An expression that describes the entire function group. Member symbols may contain symbols within them.
    const std::shared_ptr<Expression> constructExpression( const std::shared_ptr<Task>& t, 
                                                           const std::vector<std::shared_ptr<Graph::Inst>>& insts, 
                                                           const std::shared_ptr<ReductionVariable>& rv, 
                                                           const std::set<std::shared_ptr<Collection>>& colls,
                                                           const std::set<std::shared_ptr<InductionVariable>>& vars );
    /// @brief Creates expressions from collections and function nodes
    ///
    /// Expressions use the collections of functions and the found collections of data to generate the rhs of a function
    /// Steps:
    /// 1. Group each functional expression together // find all rhs expressions
    /// 2. For each function group // replace instructions in the rhs expression with collections
    ///    - for each instruction in the group
    ///      -- figure out which collection supplies this instruction (if any)
    /// 3. Construct expressions from function
    /// @param DG 
    /// @param colls 
    /// @return 
    std::shared_ptr<Expression> getExpression( const std::shared_ptr<Task>& t, 
                                               const std::set<std::shared_ptr<Collection>>& colls, 
                                               const std::set<std::shared_ptr<ReductionVariable>>& rvs,
                                               const std::set<std::shared_ptr<InductionVariable>>& vars );
} // namespace Cyclebite::Grammar