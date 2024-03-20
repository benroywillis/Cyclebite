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
    class ReductionVariable;
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
        /// @brief Returns all reduction variables involved in this expression
        /// @return A set of each reduction variable object found in the expression
        const std::set<std::shared_ptr<ReductionVariable>> getRVs() const;
        /// @brief Indicates whether this reduction can be done in parallel
        ///
        /// This method analyses where the reduction variable is stored to its output to determine parallel operation
        /// For example, when a single address stores the reduction variable during its cycle instance, that reduction is considered parallel
        /// (This operation does not type-check - it assumes full associativity of operations no matter the underlying op)
        /// However, if the reduction is stored to a different address in each iteration, that reduction is not parallel
        bool hasParallelReduction() const;
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
        /// @brief Returns the dimensions of the expression output, if any
        const std::vector<std::shared_ptr<Dimension>> getOutputDimensions() const;
        /// @brief Dumps the entire expression recursively, meaning everything is fully inlined
        ///
        /// When this method is called, each symbol in the expression is recursively constructed starting with its constituents.
        /// This is not suitable for Halide since each pipe stage needs to be isolated for proper scheduling.
        /// @return A string representation of the expression that is readable to humans.
        std::string dump() const override;
        /// @brief Dumps the entire expression recursively, meaning everything is fully inlined
        ///
        /// Not all symbols within the expression are treated equally within this method.
        /// Expressions: are only printed as a reference. This ensures that expressions within expressions are not fully inlined.
        /// All other symbols: are fully inlined and mapped within the Symbol2Symbol map.
        /// @param symbol2Symbol A 2D map that transforms symbols into other symbols. Useful for when transformations on symbols need to occur (for example, bounding the input collections with Halide::BoundaryConditions::repeat_edge). Any Symbol that is transformed in this map is automatically assumed to be an input to the expression (useful for when the input to an expression comes from the previous pipe stage). Thus, when a subexpression is present in this map, its reference is printed in the Halide expression - otherwise its entire statement is printed.
        /// @return A string representation of the expression that is compatible with the Halide front-end.
        std::string dumpHalide( const std::map<std::shared_ptr<Symbol>, std::shared_ptr<Symbol>>& symbol2Symbol ) const override;
        /// @brief Dumps a reference to this expression, including its dimensions
        ///
        /// Useful when referring to another expression within an expression. This method does not fully inline the expression, thus isolating the pipestage's work
        /// @param symbol2Symbol A 2D map that is recursively explored for mapping of symbols within the expression. 
        /// @return A string representation of the expression compatible with the Halide front-end.
        std::string dumpHalideReference( const std::map<std::shared_ptr<Symbol>, std::shared_ptr<Symbol>>& symbol2Symbol ) const;
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
    class ConstantSymbol;
    std::vector<std::shared_ptr<Expression>> getExpressions( const std::shared_ptr<Task>& t, 
                                                             const std::set<std::shared_ptr<Collection>>& colls, 
                                                             const std::set<std::shared_ptr<ReductionVariable>>& rvs,
                                                             const std::set<std::shared_ptr<ConstantSymbol>>& cons,
                                                             const std::set<std::shared_ptr<InductionVariable>>& vars );
} // namespace Cyclebite::Grammar