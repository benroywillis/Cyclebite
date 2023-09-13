#pragma once
#include "Collection.h"
#include "Graph/inc/Inst.h"

namespace Cyclebite::Grammar
{
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
        Expression( const std::vector<std::shared_ptr<Symbol>>& s, const std::vector<Cyclebite::Graph::Operation>& o );
        /// @brief Inheritor constructur 
        Expression( const std::vector<std::shared_ptr<Symbol>> s, const std::vector<Cyclebite::Graph::Operation> o, const std::string name );
        ~Expression() = default;
        const std::vector<std::shared_ptr<Symbol>>& getSymbols() const;
        const std::set<std::shared_ptr<InductionVariable>>& getVars() const;
        const std::set<std::shared_ptr<Collection>> getCollections() const;
        const std::set<std::shared_ptr<Collection>>& getInputs() const;
        const std::set<std::shared_ptr<Collection>>& getOutputs() const;
        std::string dump() const override;
    protected:
        // contains all collections referenced by this expression
        std::set<std::shared_ptr<Collection>> inputs;
        std::set<std::shared_ptr<Collection>> outputs;
        // contains the IVs that define the iterator space of this expression
        std::set<std::shared_ptr<InductionVariable>> vars;
        // contains operators that lie in between each symbol entry. Will always be of size (symbols.size() - 1)
        std::vector<Cyclebite::Graph::Operation> ops;
        // contains the symbols, in op order, for the expression. Will always be of size (ops.size() + 1)
        std::vector<std::shared_ptr<Symbol>> symbols;
        static bool printedName;
    };
    class ReductionVariable;
    const std::shared_ptr<Expression> constructExpression( const std::shared_ptr<Task>& t, const std::vector<std::shared_ptr<Graph::Inst>>& insts, const std::shared_ptr<ReductionVariable>& rv, const std::set<std::shared_ptr<Collection>>& colls );
} // namespace Cyclebite::Grammar