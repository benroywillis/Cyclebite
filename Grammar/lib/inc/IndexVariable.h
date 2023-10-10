//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include "Symbol.h"
#include "Polyhedral.h"
#include "Graph/inc/Inst.h"

namespace Cyclebite::Grammar
{
    class BasePointer;
    class InductionVariable;
    class IndexVariable : public Symbol
    {
    public:
        IndexVariable( const std::shared_ptr<Cyclebite::Graph::Inst>& n,
                       const std::set<std::shared_ptr<Cyclebite::Grammar::IndexVariable>>& p = std::set<std::shared_ptr<IndexVariable>>(), 
                       const std::set<std::shared_ptr<Cyclebite::Grammar::IndexVariable>>& c = std::set<std::shared_ptr<IndexVariable>>(),
                       bool il = false );
        IndexVariable( const std::shared_ptr<Cyclebite::Graph::Inst>& n,
                       const std::shared_ptr<Cyclebite::Grammar::IndexVariable>& p, 
                       const std::set<std::shared_ptr<Cyclebite::Grammar::IndexVariable>>& c = std::set<std::shared_ptr<IndexVariable>>(),
                       bool il = false );
        void addParent( const std::shared_ptr<Cyclebite::Grammar::IndexVariable>& p);
        void addChild( const std::shared_ptr<Cyclebite::Grammar::IndexVariable>& c);
        void setIV( const std::shared_ptr<Cyclebite::Grammar::InductionVariable>& iv);
        void addBP( const std::shared_ptr<Cyclebite::Grammar::BasePointer>& bp);
        void setLoaded( bool load );
        const std::shared_ptr<Cyclebite::Graph::Inst>& getNode() const;
        /// @brief Method returns gep(s) the indexVariable is used within
        ///
        /// When an IndexVariable maps to a binary operator, it may be used in one or more geps to offset a base pointer
        /// In order to correctly map the IndexVariable to its base pointer(s), the geps that use the IndexVariable must be found easily and used in the search to connect the two
        /// Note: this method only returns the gep(s) that immediately use this index variable - follow-on geps that may use the result of this gep are not captured 
        const std::set<std::shared_ptr<Cyclebite::Graph::Inst>, Graph::p_GNCompare> getGeps() const;
        const std::set<std::shared_ptr<Cyclebite::Grammar::IndexVariable>>& getParents() const;
        const std::set<std::shared_ptr<Cyclebite::Grammar::IndexVariable>>& getChildren() const;
        const std::shared_ptr<Cyclebite::Grammar::InductionVariable>& getIV() const;
        const std::set<std::shared_ptr<Cyclebite::Grammar::BasePointer>>& getBPs() const;
        const PolySpace getSpace() const;
        std::string dump() const override;
        bool isLoaded() const;
    protected:
        std::shared_ptr<Cyclebite::Graph::Inst> node;
        /// Index variable makes a metaphorical edge between this index variable and the control space (which is useful for constructing a polyhedral space)
        std::shared_ptr<InductionVariable> iv;
        /// Base pointers map this idxVar to the base pointer(s) it offsets
        std::set<std::shared_ptr<BasePointer>> bps;
        /// the parent idxVar is one dimension above this one
        std::set<std::shared_ptr<Cyclebite::Grammar::IndexVariable>> parents;
        /// the child idxVar is one dimension below this one
        std::set<std::shared_ptr<Cyclebite::Grammar::IndexVariable>> children;        
        /// the affine dimensions of this index
        PolySpace space;
        /// Patch on the fact that some idxVars do not have to be leaves in the idxVar tree in order to form a collection
        /// When an idxVar represents the last offset to a collection, but that idxVar is also offset by things like idxVar-1, then both this idxVar and its offset have to form collections
        /// But, the original idxVar is not a leaf in the idxVar tree
        /// This flag tells the Collection builder to make a collection for this idxVar (and its parents) even though it is not a leaf
        bool IL;
    };

    // sorts idxVars in hierarchical order (parent-most first, child-most last)
    struct idxVarHierarchySort
    {
        bool operator()(const std::shared_ptr<IndexVariable>& lhs, const std::shared_ptr<IndexVariable>& rhs) const 
        {
            if( lhs->getChildren().find(rhs) != lhs->getChildren().end() )
            {
                // this is my child, I get sorted first
                return true;
            }
            else if( !lhs->getParents().empty() && rhs->getParents().empty() )
            {
                // I have no parent and rhs does, I go first
                return true;
            }
            else if( !lhs->getChildren().empty() )
            {
                for( const auto& child : lhs->getChildren() )
                {
                    if( child->getChildren().find(rhs) != child->getChildren().end() )
                    {
                        // rhs is a child of my child, I go first
                        return true;
                    }
                }
            }
            // I can't determine whether I go before rhs without significant recursion, so just return false 
            return false;
        }
    };

    class Task;
    std::set<std::shared_ptr<IndexVariable>> getIndexVariables(const std::shared_ptr<Task>& t, 
                                                               const std::set<std::shared_ptr<BasePointer>>& BPs, 
                                                               const std::set<std::shared_ptr<InductionVariable>>& vars);
} // namespace Cyclebite::Grammar