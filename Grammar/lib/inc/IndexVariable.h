#pragma once
#include "Symbol.h"
#include "Polyhedral.h"
#include "Graph/inc/DataValue.h"

namespace Cyclebite::Grammar
{
    class BasePointer;
    class InductionVariable;
    class IndexVariable : public Symbol
    {
    public:
        IndexVariable( const std::shared_ptr<Cyclebite::Graph::DataValue>& n,
                       const std::shared_ptr<Cyclebite::Grammar::IndexVariable>& p = nullptr, 
                       const std::shared_ptr<Cyclebite::Grammar::IndexVariable>& c = nullptr );
        void setParent( const std::shared_ptr<Cyclebite::Grammar::IndexVariable>& p);
        void setChild( const std::shared_ptr<Cyclebite::Grammar::IndexVariable>& c);
        void setIV( const std::shared_ptr<Cyclebite::Grammar::InductionVariable>& iv);
        void setBP( const std::shared_ptr<Cyclebite::Grammar::BasePointer>& bp);
        const std::shared_ptr<Cyclebite::Graph::DataValue>& getNode() const;
        const std::shared_ptr<Cyclebite::Grammar::IndexVariable>& getParent() const;
        const std::shared_ptr<Cyclebite::Grammar::IndexVariable>& getChild() const;
        const std::shared_ptr<Cyclebite::Grammar::InductionVariable>& getIV() const;
        const std::shared_ptr<Cyclebite::Grammar::BasePointer>& getBP() const;
        const PolySpace getSpace() const;
        std::string dump() const override;
    protected:
        std::shared_ptr<Cyclebite::Graph::DataValue> node;
        /// Index variable makes a metaphorical edge between this index variable and the control space (which is useful for constructing a polyhedral space)
        std::shared_ptr<InductionVariable> iv;
        /// Base pointer maps this idxVar to the base pointer it offsets
        std::shared_ptr<BasePointer> bp;
        /// the parent idxVar is one dimension above this one
        std::shared_ptr<Cyclebite::Grammar::IndexVariable> parent;
        /// the child idxVar is one dimension below this one
        std::shared_ptr<Cyclebite::Grammar::IndexVariable> child;        
        /// the affine dimensions of this index
        PolySpace space;
    };

    class Task;
    std::set<std::shared_ptr<IndexVariable>> getIndexVariables(const std::shared_ptr<Task>& t, 
                                                               const std::set<std::shared_ptr<BasePointer>>& BPs, 
                                                               const std::set<std::shared_ptr<InductionVariable>>& vars);
} // namespace Cyclebite::Grammar