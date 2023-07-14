#pragma once
#include "ControlBlock.h"
#include "DataGraph.h"
#include <llvm/IR/Instructions.h>
#include <nlohmann/json.hpp>

namespace TraceAtlas::Grammar
{
    std::set<std::shared_ptr<class Cycle>> ConstructCycles(const nlohmann::json& instanceJson, 
                                                           const nlohmann::json& kernelJson, 
                                                           const std::map<int64_t, llvm::BasicBlock*>& IDToBlock,
                                                           std::set<std::shared_ptr<Cycle>>& taskCycles);
    class Cycle
    {
    public:
        Cycle() = default;
        Cycle(const llvm::BranchInst* c, const std::set<std::shared_ptr<TraceAtlas::Graph::ControlBlock>, TraceAtlas::Graph::p_GNCompare>& b) : iteratorInst(c), blocks(b) {}
        const llvm::BranchInst* getIteratorInst() const;
        const std::set<std::shared_ptr<class Cycle>>& getChildren() const;
        const std::set<std::shared_ptr<class Cycle>>& getParents() const;
        const std::set<std::shared_ptr<TraceAtlas::Graph::ControlBlock>, TraceAtlas::Graph::p_GNCompare>& getBody() const;
        bool find(const std::shared_ptr<TraceAtlas::Graph::DataNode>& n) const;
        bool find(const std::shared_ptr<TraceAtlas::Graph::ControlBlock>& b) const;
        void addChild(const std::shared_ptr<class Cycle>& c);
        void addParent(const std::shared_ptr<class Cycle>& p);
    private:
        const llvm::BranchInst* iteratorInst; // right now, the belief is that each task should have exactly one comparator that decides its next iteration
        std::set<std::shared_ptr<TraceAtlas::Graph::ControlBlock>, TraceAtlas::Graph::p_GNCompare> blocks;
        std::set<std::shared_ptr<class Cycle>> children;
        std::set<std::shared_ptr<class Cycle>> parents;
    };
} // namespace TraceAtlas::Grammar