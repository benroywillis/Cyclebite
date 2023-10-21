//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include "Symbol.h"
#include "Polyhedral.h"
#include "Graph/inc/Inst.h"
#include <deque>

namespace Cyclebite::Grammar
{
    class BasePointer;
    class InductionVariable;

    /// Holds the offset characteristics done on a dimension by an index variable
    struct DimensionOffset
    {
        Cyclebite::Graph::Operation op;
        // coefficients must be integers, if they aren't that is an error (you should never do a floating point op on a pointer)
        int coefficient;
    };

    class IndexVariable : public Symbol
    {
    public:
        IndexVariable( const std::shared_ptr<Cyclebite::Graph::Inst>& n,
                       const std::set<std::shared_ptr<Cyclebite::Grammar::IndexVariable>>& p = std::set<std::shared_ptr<IndexVariable>>(), 
                       const std::set<std::shared_ptr<Cyclebite::Grammar::IndexVariable>>& c = std::set<std::shared_ptr<IndexVariable>>() );
        IndexVariable( const std::shared_ptr<Cyclebite::Graph::Inst>& n,
                       const std::shared_ptr<Cyclebite::Grammar::IndexVariable>& p, 
                       const std::set<std::shared_ptr<Cyclebite::Grammar::IndexVariable>>& c = std::set<std::shared_ptr<IndexVariable>>() );
        void addParent( const std::shared_ptr<Cyclebite::Grammar::IndexVariable>& p);
        void addChild( const std::shared_ptr<Cyclebite::Grammar::IndexVariable>& c);
        void addIV( const std::shared_ptr<Cyclebite::Grammar::InductionVariable>& iv);
        void setIndexBP( const std::shared_ptr<Cyclebite::Grammar::BasePointer>& p);
        void addOffsetBP( const std::shared_ptr<Cyclebite::Grammar::BasePointer>& p );
        const std::shared_ptr<Cyclebite::Graph::Inst>& getNode() const;
        /// @brief Method returns gep(s) the indexVariable is used within
        ///
        /// When an IndexVariable maps to a binary operator, it may be used in one or more geps to offset a base pointer
        /// In order to correctly map the IndexVariable to its base pointer(s), the geps that use the IndexVariable must be found easily and used in the search to connect the two
        /// Note: this method only returns the gep(s) that immediately use this index variable - follow-on geps that may use the result of this gep are not captured 
        const std::set<std::shared_ptr<Cyclebite::Graph::Inst>, Graph::p_GNCompare> getGeps() const;
        const std::set<std::shared_ptr<Cyclebite::Grammar::IndexVariable>>& getParents() const;
        const std::set<std::shared_ptr<Cyclebite::Grammar::IndexVariable>>& getChildren() const;
        const std::set<std::shared_ptr<Cyclebite::Grammar::InductionVariable>>& getIVs() const;
        const std::set<std::shared_ptr<Cyclebite::Grammar::BasePointer>>& getOffsetBPs() const;
        const PolySpace getSpace() const;
        std::string dump() const override;
        /// @brief Returns true if the given value is the index variable or a transformed version of it
        ///
        /// The method will only search until one of two conditions are satisfied
        /// 1. The value has been found to be a user (or user of users) of the index variable
        /// 2. Another index variable has been hit
        /// Thus, this method will not return true if the input argument is the use of one of the index variable's children. The input argument must be a "direct" use of the index variable.
        /// @return True if the given value is the index variable or a transformed version of it (for example, casted). False otherwise.
        bool isValueOrTransformedValue(const llvm::Value* v) const;
        /// @brief Return true if this index variable represents a dimension of its base pointer
        ///
        /// A dimension is an array index + its affine transformations to map it to a flattened memory space.
        /// It is analogous to a "var" in Halide
        /// - an array index is most commonly an phi node
        /// - Dimension example: phi0 -> mul(phi, SIZE) -> add(phi1) -> array access (phi0+mul(SIZE) is a dimension, phi1 is another dimension)
        /// e.g., a[i][j] -> i and j are dimensions of a
        /// e.g., a[i][j-1] -> i and j are dimensions of a, but -1 is not a dimension of a
        /// e.g., a[1][i][j] -> 1 is NOT a dimension of a, i and j are (1 is just a constant offset that cannot be transformed onto a flattened memory space - it is an artifact of the programmer's data structure)
        bool isDimension() const;
        /// @brief Returns the dimension that is offset by this indexVariable
        ///
        /// @return The indexVariable that represents the dimension this indexVariable interacts with. If this indexVariable is a dimension that doesn't have a parent, an empty set is returned.
        const std::set<std::shared_ptr<IndexVariable>> getOffsetDimensions() const;
        /// @brief Returns whether these two index variables overlap in their dimension
        ///
        /// When two index variables index the same dimension of the base pointer, this has implications on what they do
        /// @return True if these two index variables overlap in the dimension they touch, false otherwise
        bool overlaps( const std::shared_ptr<class IndexVariable>& var2 ) const;
        /// @brief Returns the operation and offset done on a dimension by this idxVar
        ///
        /// If the idxVar is a dimension itself, it returns an empty operator and offset
        /// @return A DimensionOffset object with the operation and offset this IndexVariable does on its dimension
        DimensionOffset getOffset() const;
        /// Returns the dimension this index variable accesses in its base pointer
        int getDimensionIndex() const;
    protected:
        std::shared_ptr<Cyclebite::Graph::Inst> node;
        /// Index variable makes a metaphorical edge between this index variable and the control space (which is useful for constructing a polyhedral space)
        std::set<std::shared_ptr<InductionVariable>> iv;
        /// Base pointer that yields the value we offset our BP with
        std::set<std::shared_ptr<BasePointer>> offsetBPs;
        /// the parent idxVar is one dimension above this one
        std::set<std::shared_ptr<Cyclebite::Grammar::IndexVariable>> parents;
        /// the child idxVar is one dimension below this one
        std::set<std::shared_ptr<Cyclebite::Grammar::IndexVariable>> children;        
        /// the affine dimensions of this index
        PolySpace space;
    };

    // sorts idxVars in hierarchical order (parent-most first, child-most last)
    // in the default stl::set, things are sorted from "least" to "greatest", meaning lhs < rhs being "true" puts lhs before rhs in the set
    // thus, to get parent-most first, lhs is "true" when it is the parent of rhs and false otherwise
    struct idxVarHierarchySort
    {
        bool operator()(const std::shared_ptr<IndexVariable>& lhs, const std::shared_ptr<IndexVariable>& rhs) const 
        {
            if( lhs == rhs )
            {
                // if lhs is rhs, they are equal, and std::sets take entries whose comparisons are always false to be equal
                return false;
            }
            if( lhs->getChildren().find(rhs) != lhs->getChildren().end() )
            {
                // this is my child, I get sorted first
                return true;
            }
            if( lhs->getParents().empty() && !rhs->getParents().empty() )
            {
                // I have no parent and rhs does, I go first
                return true;
            }
            else 
            {
                std::deque<std::shared_ptr<IndexVariable>> Q;
                std::set<std::shared_ptr<IndexVariable>> covered;
                Q.push_front(lhs);
                // we assume lhs is the parent of rhs and confirm it with a BFS of the children of the idxTree, starting from lhs
                while( !Q.empty() )
                {
                    for( const auto& c : Q.front()->getChildren() )
                    {
                        if( c == rhs )
                        {
                            // confirmed, lhs is rhs's parent
                            return true;
                        }
                        else
                        {
                            if( !covered.contains(c) )
                            {
                                Q.push_back(c);
                                covered.insert(c);
                            }
                        }
                    }
                    Q.pop_front();
                }
            }            
            // if we've made it this far, we have confirmed lhs is NOT the parent of rhs, so return false 
            return false;
        }
    };

    class Task;
    std::set<std::shared_ptr<IndexVariable>> getIndexVariables(const std::shared_ptr<Task>& t, 
                                                               const std::set<std::shared_ptr<BasePointer>>& BPs, 
                                                               const std::set<std::shared_ptr<InductionVariable>>& vars);
} // namespace Cyclebite::Grammar