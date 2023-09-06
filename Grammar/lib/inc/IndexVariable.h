#pragma once
#include "Symbol.h"
#include "Graph/inc/DataValue.h"
#include <limits.h>

namespace Cyclebite::Grammar
{
    /// @brief Defines stride patterns
    enum class StridePattern
    {
        Sequential,
        Random
    };

    struct PolySpace
    {
        uint32_t min;
        uint32_t max;
        uint32_t stride;
        StridePattern pattern;
    };
    
    enum class IV_BOUNDARIES 
    {
        INVALID = INT_MIN,
        UNDETERMINED = INT_MIN+1
    };

    struct AffineOffset
    {
        int constant;
        Graph::Operation transform;
    };

    class IndexVariable : public Symbol
    {
    public:
        IndexVariable( const std::shared_ptr<Cyclebite::Graph::DataValue>& n );
        const std::shared_ptr<Cyclebite::Graph::DataValue>& getNode() const;
        const std::shared_ptr<Cyclebite::Grammar::IndexVariable>& getParent() const;
        const std::shared_ptr<Cyclebite::Grammar::IndexVariable>& getChild() const;
        const PolySpace getSpace() const;
        std::string dump() const override;
    protected:
        std::shared_ptr<Cyclebite::Graph::DataValue> node;
        /// the parent idxVar is one dimension above this one
        std::shared_ptr<Cyclebite::Grammar::IndexVariable> parent;
        /// the child idxVar is one dimension below this one
        std::shared_ptr<Cyclebite::Grammar::IndexVariable> child;        
        /// the affine dimensions of this index
        PolySpace space;
    };
} // namespace Cyclebite::Grammar