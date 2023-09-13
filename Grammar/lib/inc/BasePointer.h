#pragma once
#include "Symbol.h"
#include "DataValue.h"
#include <llvm/IR/Instructions.h>

namespace Cyclebite::Grammar
{
    /// @brief Sets the threshold, in bytes, that a memory allocation must make in order to be considered a base pointer
    constexpr uint64_t ALLOC_THRESHOLD = 128;
    /// @brief Decides whether this function allocates memory
    /// @param call     The call instruction
    /// @return The number of bytes allocated by the call. If the call instruction does not allocate memory, it returns 0.
    uint32_t isAllocatingFunction(const llvm::CallBase* call);
    /// @brief Finds the source of a pointer operand
    /// @param ptr      The pointer to be investigated
    /// @return The source of the pointer. If no source could be determined, the input arg is returned;
    const llvm::Value* getPointerSource(const llvm::Value* ptr);
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
        bool isOffset( const llvm::Value* val ) const;
    private:
        std::shared_ptr<Cyclebite::Graph::DataValue> node;
        std::vector<std::pair<const llvm::GetElementPtrInst*, const llvm::LoadInst*>> loads;
        std::vector<std::pair<const llvm::GetElementPtrInst*, const llvm::StoreInst*>> stores;
    };
} // namespace Cyclebite::Grammar