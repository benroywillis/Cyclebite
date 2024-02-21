//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include "InductionVariable.h"
#include "Graph/inc/Inst.h"

namespace Cyclebite::Grammar
{
    class BasePointer;

    /// Holds the offset characteristics done on a dimension by an index variable
    struct DimensionOffset
    {
        Cyclebite::Graph::Operation op;
        // coefficients must be integers, if they aren't that is an error (you should never do a floating point op on a pointer)
        int coefficient;
    };

    /// @brief This class represents accesses into memory
    ///
    /// There are generally two ways that memory accesses are modeled within LLVM IR
    /// 1. A constant integer is used within a gep instruction
    ///    - commonly used to index into statically-defined structures
    /// 2. An affine transformation of a dimension
    ///    - consider the case where an index variable is used to index memory
    ///      -- e.g., for i on I, x[i] = y[i]
    ///    - then if we wanted to take every other it would be
    ///      -- for i on I/2, x[i] = y[i*2]
    ///    - an index variable would model i*2 for its transform of the dimension i, thus modeling the memory access pattern of that expression
    /// Both of these cases have their own implications, and must be accounted for when identifying this index variable uniquely (see descriptions on the node and inst class memebers)
    class IndexVariable : public Symbol
    {
    public:
        IndexVariable( const std::shared_ptr<Cyclebite::Graph::DataValue>& n,
                       const std::shared_ptr<Cyclebite::Graph::Inst>& i,
                       const std::set<std::shared_ptr<Cyclebite::Grammar::IndexVariable>>& p = std::set<std::shared_ptr<IndexVariable>>(), 
                       const std::set<std::shared_ptr<Cyclebite::Grammar::IndexVariable>>& c = std::set<std::shared_ptr<IndexVariable>>() );
        IndexVariable( const std::shared_ptr<Cyclebite::Graph::DataValue>& n,
                       const std::shared_ptr<Cyclebite::Graph::Inst>& i,
                       const std::shared_ptr<Cyclebite::Grammar::IndexVariable>& p, 
                       const std::set<std::shared_ptr<Cyclebite::Grammar::IndexVariable>>& c = std::set<std::shared_ptr<IndexVariable>>() );
        void addParent( const std::shared_ptr<Cyclebite::Grammar::IndexVariable>& p);
        void addChild( const std::shared_ptr<Cyclebite::Grammar::IndexVariable>& c);
        void addDimension( const std::shared_ptr<Cyclebite::Grammar::Dimension>& iv);
        void addOffsetBP( const std::shared_ptr<Cyclebite::Grammar::BasePointer>& p );
        const std::shared_ptr<Cyclebite::Graph::DataValue>& getNode() const;
        const std::shared_ptr<Cyclebite::Graph::Inst>& getInst() const;
        /// @brief Method returns gep(s) the indexVariable is used within
        ///
        /// When an IndexVariable maps to a binary operator, it may be used in one or more geps to offset a base pointer
        /// In order to correctly map the IndexVariable to its base pointer(s), the geps that use the IndexVariable must be found easily and used in the search to connect the two
        /// Note: this method only returns the gep(s) that immediately use this index variable - follow-on geps that may use the result of this gep are not captured 
        const std::set<std::shared_ptr<Cyclebite::Graph::Inst>, Graph::p_GNCompare> getGeps() const;
        const std::set<std::shared_ptr<Cyclebite::Grammar::IndexVariable>>& getParents() const;
        const std::set<std::shared_ptr<Cyclebite::Grammar::IndexVariable>>& getChildren() const;
        /// @brief Returns all dimensions that this indexVariable may use
        const std::set<std::shared_ptr<Cyclebite::Grammar::Dimension>>& getDimensions() const;
        /// @brief Returns only the dimension associated with this indexVariable
        ///
        /// This method used the indexVariable tree to find the dimensions that are exclusive to this index variable
        /// An "exclusive" dimension is one which does not belong to any parents of this index variable - it is unique to this node in the hierarchy tree
        /// Note: this method may return an empty set if it cannot find any exclusive dimensions for this indexVariable
        /// @return Only the dimensions that are exclusive to this indexVariable (according to the indexVariable hierarchy). If no exclusive dimensions can be found, this set is empty
        const std::set<std::shared_ptr<Cyclebite::Grammar::Dimension>> getExclusiveDimensions() const;
        const std::set<std::shared_ptr<Cyclebite::Grammar::BasePointer>>& getOffsetBPs() const;
        const PolySpace getSpace() const;
        std::string dump() const override;
        std::string dumpHalide( const std::map<std::shared_ptr<Symbol>, std::shared_ptr<Symbol>>& symbol2Symbol ) const override;
        /// @brief Returns true if the given value is the index variable or a transformed version of it
        ///
        /// The method will only search until one of two conditions are satisfied
        /// 1. The value has been found to be a user (or user of users) of the index variable
        /// 2. Another index variable has been hit
        /// Thus, this method will not return true if the input argument is the use of one of the index variable's children. The input argument must be a "direct" use of the index variable.
        /// @return True if the given value is the index variable or a transformed version of it (for example, casted). False otherwise.
        bool isValueOrTransformedValue( const llvm::Instruction* i, const llvm::Value* v) const;
        /// @brief Returns whether these two index variables overlap in their dimension
        ///
        /// When two index variables index the same dimension of the base pointer, this has implications on what they do
        /// @return True if these two index variables overlap in the dimension they touch, false otherwise
        bool overlaps( const std::shared_ptr<class IndexVariable>& var1 ) const;
        /// @brief Returns the operation and offset done on a dimension by this idxVar
        ///
        /// If the idxVar is a dimension itself, it returns an empty operator and offset
        /// @return A DimensionOffset object with the operation and offset this IndexVariable does on its dimension
        DimensionOffset getOffset() const;
        /// @brief Returns the dimension this index variable accesses in its base pointer
        ///
        /// In the case that GEPs offset a pointer that doesn't come from a user-defined struct, this will return -1 because there are no dimensions to report
        /// @return A non-negative integer if the dimension index is valid. If invalid, a negative integer.
        int getDimensionIndex() const;
    protected:
        /// @brief The node is the value that this index variable refers to
        /// If this index variable represents a constant within a gep, this is the constant
        /// If this index variable represents an affine transform of a dimension, this is that affine transform instruction (and thus node and inst are the same thing)
        std::shared_ptr<Cyclebite::Graph::DataValue> node;
        /// @brief The inst represents the instruction having to do with the index variable
        ///
        /// If the index variable models a constant within a gep, this is the gep
        /// If the index variable models an affine transform of a dimension, this that affine transform instruction (thus node and inst will be the same thing)
        std::shared_ptr<Cyclebite::Graph::Inst> inst;
        /// Index variable makes a metaphorical edge between this index variable and the control space (which is useful for constructing a polyhedral space)
        std::set<std::shared_ptr<Dimension>> dims;
        /// Base pointer that yields the value we offset our BP with
        std::set<std::shared_ptr<BasePointer>> offsetBPs;
        /// the parent idxVar is one dimension above this one
        std::set<std::shared_ptr<Cyclebite::Grammar::IndexVariable>> parents;
        /// the child idxVar is one dimension below this one
        std::set<std::shared_ptr<Cyclebite::Grammar::IndexVariable>> children;        
        /// the affine dimensions of this index
        PolySpace space;
    private:
        /// Helper function that needs access to class members
        std::string printIdxVar(const std::map<std::shared_ptr<Symbol>, std::shared_ptr<Symbol>>& symbol2Symbol, const std::shared_ptr<InductionVariable>& var ) const;
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
    std::set<std::shared_ptr<IndexVariable>> getIndexVariables(const std::shared_ptr<Task>& t, const std::set<std::shared_ptr<InductionVariable>>& vars);
} // namespace Cyclebite::Grammar