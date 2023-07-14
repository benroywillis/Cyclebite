#pragma once
#include "BasePointer.h"
#include "InductionVariable.h"
#include "Symbol.h"

namespace Cyclebite::Grammar
{
    class Collection : public Symbol
    {
    public:
        Collection(const std::shared_ptr<BasePointer>& p, const std::vector<std::shared_ptr<InductionVariable>>& v ) : Symbol("collection"), vars(v), bp(p) {}
        ~Collection() = default;
        uint32_t getNumDims() const;
        const std::shared_ptr<InductionVariable>& operator[](unsigned i) const;
        const std::shared_ptr<BasePointer>& getBP() const;
        const std::vector<std::shared_ptr<InductionVariable>>& getVars() const;
        std::string dump() const override;
    protected:
        std::vector<std::shared_ptr<InductionVariable>> vars;
        std::shared_ptr<BasePointer> bp;
    };

    /*struct CollCompare
    {
        using is_transparent = void;
        bool operator()(const std::shared_ptr<Collection>& lhs, const std::shared_ptr<Collection>& rhs ) const
        {
            return lhs->getBP() < rhs->getBP();
        }
        bool operator()(const std::shared_ptr<Collection>& lhs, const std::shared_ptr<BasePointer>& rhs) const 
        {
            return lhs->getBP() < rhs;
        }
        bool operator()(const std::shared_ptr<BasePointer>& lhs, const std::shared_ptr<Collection>& rhs) const
        {
            return lhs < rhs->getBP();
        }
    };*/
} // namespace Cyclebite::Grammar