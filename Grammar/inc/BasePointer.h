#pragma once
#include "Symbol.h"
#include "DataNode.h"
#include <llvm/IR/Instructions.h>

namespace Cyclebite::Grammar
{
    class BasePointer : public Symbol
    {
    public:
        BasePointer(const std::shared_ptr<Cyclebite::Graph::DataNode>& n, const std::vector<std::pair<const llvm::GetElementPtrInst*, const llvm::LoadInst*>>& lds, 
                    const std::vector<std::pair<const llvm::GetElementPtrInst*, const llvm::StoreInst*>>& sts) : Symbol("bp"), node(n), loads(lds), stores(sts) {}
        ~BasePointer() = default;
        const std::shared_ptr<Cyclebite::Graph::DataNode>& getNode() const;
        const std::vector<std::pair<const llvm::GetElementPtrInst*, const llvm::LoadInst*>>& getAccesses() const;
        const std::vector<std::pair<const llvm::GetElementPtrInst*, const llvm::StoreInst*>>& getStores() const;
        const std::vector<const llvm::LoadInst*> getlds() const;
        const std::vector<const llvm::StoreInst*> getsts() const;
        const std::vector<const llvm::GetElementPtrInst*> getgps() const;
    private:
        std::shared_ptr<Cyclebite::Graph::DataNode> node;
        std::vector<std::pair<const llvm::GetElementPtrInst*, const llvm::LoadInst*>> loads;
        std::vector<std::pair<const llvm::GetElementPtrInst*, const llvm::StoreInst*>> stores;
    };
} // namespace Cyclebite::Grammar