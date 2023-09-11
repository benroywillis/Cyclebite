#pragma once
#include "IndexVariable.h"
#include "Symbol.h"

namespace Cyclebite::Grammar
{
    class Collection : public Symbol
    {
    public:
        Collection( const std::set<std::shared_ptr<IndexVariable>>& v, const std::shared_ptr<BasePointer>& p );
        ~Collection() = default;
        uint32_t getNumDims() const;
        const std::shared_ptr<IndexVariable>& operator[](unsigned i) const;
        const std::shared_ptr<BasePointer>& getBP() const;
        const std::vector<std::shared_ptr<IndexVariable>>& getIndices() const;
        std::string dump() const override;
    protected:
        std::vector<std::shared_ptr<IndexVariable>> vars;
        std::shared_ptr<BasePointer> bp;
    };
} // namespace Cyclebite::Grammar