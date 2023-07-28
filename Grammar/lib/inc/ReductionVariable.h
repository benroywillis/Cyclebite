#pragma once
#include "Symbol.h"
#include "InductionVariable.h"
#include "Graph/inc/Inst.h"

namespace Cyclebite::Grammar
{
    class ReductionVariable : public Symbol
    {
    public:
        ReductionVariable(const std::shared_ptr<InductionVariable>& iv, const std::shared_ptr<Cyclebite::Graph::DataValue>& n);
        std::string dump() const override;
        Cyclebite::Graph::Operation getOp() const;
        const std::shared_ptr<Cyclebite::Graph::DataValue>& getNode() const;
    private:
        std::shared_ptr<InductionVariable> iv;
        std::shared_ptr<Cyclebite::Graph::DataValue> node;
        Cyclebite::Graph::Operation bin;
    };
} // namespace Cyclebite::Grammar