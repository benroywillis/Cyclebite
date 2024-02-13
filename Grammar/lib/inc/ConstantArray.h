//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include "ConstantSymbol.h"
#include <llvm/IR/Constant.h>
#include <vector>
#include "Graph/inc/Inst.h"

namespace Cyclebite::Grammar
{
    class IndexVariable;
    template <typename T>
    class ConstantArray : public ConstantSymbol<T>
    {
    public:
        /// @brief          The input array is copied into the structure - the caller is responsible for managing the memory of the input array
        /// @param idxVars  The index variables that index this constant array, if any. 
        /// @param a        A pointer to the array that contains the constant values of the constant array
        /// @param size     The number of elements in the input array
        ConstantArray( const std::vector<std::shared_ptr<IndexVariable>>& idxVars, T* a, int size) : ConstantSymbol<T>(*a), vars(idxVars), array(a), arraySize(size) {}
        std::string dumpHalide( const std::map<std::shared_ptr<Dimension>, std::shared_ptr<ReductionVariable>>& dimToRV ) const override;
        std::string dumpArray_C() const;
        const std::vector<std::shared_ptr<IndexVariable>>& getVars() const;
        const T* getArray() const;
        int getArraySize() const;
    private:
        /// The index variables that index the constant array, if any
        std::vector<std::shared_ptr<IndexVariable>> vars;
        /// A pointer to the array of constant values held by this constant
        T* array;
        /// The number of entries in the array
        int arraySize;
    };
    template class ConstantArray<int>;
    template class ConstantArray<int64_t>;
    template class ConstantArray<float>;
    template class ConstantArray<double>;

    std::string getArrayType(const llvm::Constant* ptr);
    void getConstant( const std::shared_ptr<Cyclebite::Graph::Inst>& opInst, 
                      const llvm::Constant* con, 
                      std::vector<std::shared_ptr<Symbol>>& newSymbols, 
                      std::map<std::shared_ptr<Cyclebite::Graph::DataValue>, std::shared_ptr<Symbol>>& nodeToExpr );
} // namespace Cyclebite::Grammar