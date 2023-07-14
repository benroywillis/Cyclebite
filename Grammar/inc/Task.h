#pragma once
#include "Cycle.h"

namespace TraceAtlas::Grammar
{
    std::set<std::shared_ptr<class Task>> getTasks(const nlohmann::json& instanceJson, 
                                                   const nlohmann::json& kernelJson, 
                                                   const std::map<int64_t, llvm::BasicBlock*>& IDToBlock);
    class Task
    {
    public:
        Task( const std::set<std::shared_ptr<Cycle>>& c) : cycles(c) {}
        const std::set<std::shared_ptr<Cycle>>& getCycles() const;
        const std::set<std::shared_ptr<Cycle>> getChildMostCycles() const;
        const std::set<std::shared_ptr<Cycle>> getParentMostCycles() const;
        bool find(const std::shared_ptr<TraceAtlas::Graph::DataNode>& n) const;
        bool find(const std::shared_ptr<TraceAtlas::Graph::ControlBlock>& b) const;
        bool find(const std::shared_ptr<Cycle>& c) const;
    private:
        std::set<std::shared_ptr<Cycle>> cycles;
    };
} // namespace TraceAtlas::Grammar