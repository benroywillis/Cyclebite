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
                       const std::shared_ptr<Cyclebite::Grammar::IndexVariable>& p = nullptr, 
                       const std::shared_ptr<Cyclebite::Grammar::IndexVariable>& c = nullptr );
        void setParent( const std::shared_ptr<Cyclebite::Grammar::IndexVariable>& p);
        void setChild( const std::shared_ptr<Cyclebite::Grammar::IndexVariable>& c);
        void setIV( const std::shared_ptr<Cyclebite::Grammar::InductionVariable>& iv);
        void addBP( const std::shared_ptr<Cyclebite::Grammar::BasePointer>& bp);
        const std::shared_ptr<Cyclebite::Graph::Inst>& getNode() const;
        /// @brief Method returns gep(s) the indexVariable is used within
        ///
        /// When an IndexVariable maps to a binary operator, it may be used in one or more geps to offset a base pointer
        /// In order to correctly map the IndexVariable to its base pointer(s), the geps that use the IndexVariable must be found easily and used in the search to connect the two
        /// Note: this method only returns the gep(s) that immediately use this index variable - follow-on geps that may use the result of this gep are not captured 
        const std::set<std::shared_ptr<Cyclebite::Graph::Inst>, Graph::p_GNCompare> getGeps() const;
        const std::shared_ptr<Cyclebite::Grammar::IndexVariable>& getParent() const;
        const std::shared_ptr<Cyclebite::Grammar::IndexVariable>& getChild() const;
        const std::shared_ptr<Cyclebite::Grammar::InductionVariable>& getIV() const;
        const std::set<std::shared_ptr<Cyclebite::Grammar::BasePointer>>& getBPs() const;
        const PolySpace getSpace() const;
        std::string dump() const override;
    protected:
        std::shared_ptr<Cyclebite::Graph::Inst> node;
        /// Index variable makes a metaphorical edge between this index variable and the control space (which is useful for constructing a polyhedral space)
        std::shared_ptr<InductionVariable> iv;
        /// Base pointers map this idxVar to the base pointer(s) it offsets
        std::set<std::shared_ptr<BasePointer>> bps;
        /// the parent idxVar is one dimension above this one
        std::shared_ptr<Cyclebite::Grammar::IndexVariable> parent;
        /// the child idxVar is one dimension below this one
        std::shared_ptr<Cyclebite::Grammar::IndexVariable> child;        
        /// the affine dimensions of this index
        PolySpace space;
    };

    // sorts idxVars in hierarchical order (parent-most first, child-most last)
    struct idxVarHierarchySort
    {
        bool operator()(const std::shared_ptr<IndexVariable>& lhs, const std::shared_ptr<IndexVariable>& rhs) const 
        {
            if( lhs->getChild() == rhs )
            {
                // this is my child, I get sorted first
                return true;
            }
            else if( (lhs->getParent() == nullptr) && (rhs->getParent() != nullptr) )
            {
                // I have no parent and rhs does, I go first
                return true;
            }
            else if( lhs->getChild() )
            {
                if( rhs == lhs->getChild()->getChild() )
                {
                    // rhs is a child of my child, I go first
                    return true;
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