#pragma once
#include "IndexVariable.h"

namespace Cyclebite::Grammar
{
    /// @brief A random access variable is an index variable that uses another data structure to get its accesses
    ///
    /// The memory array whose members are used as array indices must be a collection
    /// Beyond that collection, a RandomAccessVariable is exactly like an IndexVariable
    class RandomAccessVariable : public IndexVariable
    {
    public:
        /// @param bp The base pointer whose values are the indices produced by this random access variable
        /// @param n  The node is the load instruction that produces a value from the RandomAccessVariable's collection
        /// @param i  The Cyclebite::Graph::Inst is the instruction that produces a value from this RandomAccessVariable's collection (n === i)
        /// @param p  The parents are the index variables that are used to access a specific member within the RandomAccessVariable
        /// @param c  The children are the index variables that result from the RandomAccessVariable's inst (if any)
        RandomAccessVariable( const std::shared_ptr<Cyclebite::Grammar::BasePointer>& b,
                              const std::shared_ptr<Cyclebite::Graph::DataValue>& n,
                              const std::shared_ptr<Cyclebite::Graph::Inst>& i,
                              const std::set<std::shared_ptr<Cyclebite::Grammar::IndexVariable>>& ps = std::set<std::shared_ptr<IndexVariable>>(), 
                              const std::set<std::shared_ptr<Cyclebite::Grammar::IndexVariable>>& cs = std::set<std::shared_ptr<IndexVariable>>() ) : IndexVariable(n, i, ps, cs), bp(b) {}
        const std::shared_ptr<Cyclebite::Grammar::BasePointer>& getBasePointer() const;
    protected:
        std::shared_ptr<Cyclebite::Grammar::BasePointer> bp;
    };
} // namespace Cyclebite::Grammar