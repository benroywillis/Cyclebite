#pragma once
#include "Symbol.h"
#include "InductionVariable.h"

namespace Cyclebite::Grammar
{
    class ReductionVariable : public Symbol
    {
    public:
        ReductionVariable(const std::shared_ptr<InductionVariable>& iv, const std::shared_ptr<Cyclebite::Graph::DataNode>& n);
        std::string dump() const override;
        Cyclebite::Graph::Operation getOp() const;
        const std::shared_ptr<Cyclebite::Graph::DataNode>& getNode() const;
    private:
        std::shared_ptr<InductionVariable> iv;
        std::shared_ptr<Cyclebite::Graph::DataNode> node;
        Cyclebite::Graph::Operation bin;
    };
} // namespace Cyclebite::Grammar