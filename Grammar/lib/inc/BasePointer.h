//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include "Symbol.h"
#include "DataValue.h"
#include <llvm/IR/Instructions.h>

namespace Cyclebite::Grammar
{
    class Task;
    class FunctionCallArgs;
    /// @brief Sets the threshold, in bytes, that a memory allocation must make in order to be considered a base pointer
    constexpr uint64_t ALLOC_THRESHOLD = 128;

    /// @brief Recursively searches the static call graph for dynamic memory allocation functions
    ///
    /// The first allocation function found is the one whose allocation size is returned. 
    /// If it uses a dynamically-determined allocation size, a size of 0 is returned.
    /// The allocation functions currently supported are
    /// malloc, calloc, "new" operator (C++), and posix_memalign
    /// @param call     CallBase to investigate. The called function within this instruction will be recursed into if no allocating function is found within it
    /// @param parent   The parent function that "owns" the call parameter (it is a call instruction within parent's body). The parent's args map to determined values inside "args"
    /// @param args     Determined values that map to arguments in the "parent" function. These determined values are mapped to their uses in the parent function's body, which may include arguments in the function called by "call".
    /// @return         The size in bytes of the found dynamic memory allocation function. If no allocation function could be found, or if the allocation used a dynamically-determined size, 0 is returned.
    uint32_t isAllocatingFunction(const llvm::CallBase* call, const llvm::Function* parent = nullptr, const FunctionCallArgs* args = nullptr );
    /// @brief Finds the source of a pointer operand
    /// @param ptr      The pointer to be investigated
    /// @return         The source of the pointer. If no source could be determined, the input arg is returned;
    const llvm::Value* getPointerSource(const llvm::Value* ptr);
    class BasePointer : public Symbol
    {
    public:
        BasePointer(const std::shared_ptr<Cyclebite::Graph::DataValue>& n) : Symbol("bp"), node(n) {}
        ~BasePointer() = default;
        const std::shared_ptr<Cyclebite::Graph::DataValue>& getNode() const;
        bool isOffset( const llvm::Value* val ) const;
        const llvm::Type* getContainedType() const;
        const std::string getContainedTypeString() const;
    private:
        std::shared_ptr<Cyclebite::Graph::DataValue> node;
    };
    std::set<std::shared_ptr<BasePointer>> getBasePointers(const std::shared_ptr<Task>& t);
} // namespace Cyclebite::Grammar