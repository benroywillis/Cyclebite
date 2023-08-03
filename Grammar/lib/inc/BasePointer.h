#pragma once
#include "Symbol.h"
#include "DataValue.h"
#include <llvm/IR/Instructions.h>

namespace Cyclebite::Grammar
{
    /// @brief Sets the threshold, in bytes, that a memory allocation must make in order to be considered a base pointer
    constexpr uint64_t ALLOC_THRESHOLD = 128;
    uint32_t isAllocatingFunction(const llvm::CallBase* call);
    class BasePointer : public Symbol
    {
    public:
        BasePointer(const std::shared_ptr<Cyclebite::Graph::DataValue>& n, const std::vector<std::pair<const llvm::GetElementPtrInst*, const llvm::LoadInst*>>& lds, 
                    const std::vector<std::pair<const llvm::GetElementPtrInst*, const llvm::StoreInst*>>& sts) : Symbol("bp"), node(n), loads(lds), stores(sts) {}
        ~BasePointer() = default;
        const std::shared_ptr<Cyclebite::Graph::DataValue>& getNode() const;
        const std::vector<std::pair<const llvm::GetElementPtrInst*, const llvm::LoadInst*>>& getAccesses() const;
        const std::vector<std::pair<const llvm::GetElementPtrInst*, const llvm::StoreInst*>>& getStores() const;
        const std::vector<const llvm::LoadInst*> getlds() const;
        const std::vector<const llvm::StoreInst*> getsts() const;
        const std::vector<const llvm::GetElementPtrInst*> getgps() const;
    private:
        std::shared_ptr<Cyclebite::Graph::DataValue> node;
        std::vector<std::pair<const llvm::GetElementPtrInst*, const llvm::LoadInst*>> loads;
        std::vector<std::pair<const llvm::GetElementPtrInst*, const llvm::StoreInst*>> stores;
    };
} // namespace Cyclebite::Grammar