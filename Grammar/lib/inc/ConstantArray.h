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
    class ConstantArray : public ConstantSymbol
    {
    public:
        /// @brief          The input array is copied into the structure - the caller is responsible for managing the memory of the input array
        /// @param idxVars  The index variables that index this constant array, if any. 
        /// @param a        A pointer to the array that contains the constant values of the constant array
        /// @param size     The number of elements in the input array
        ConstantArray( const std::vector<std::shared_ptr<IndexVariable>>& idxVars, void* a, ConstantType type, int size) : ConstantSymbol(a, type), vars(idxVars), arraySize(size) 
        {
            switch(t)
            {
                case ConstantType::SHORT: 
                {
                    array = new short[(unsigned)size];
                    for( unsigned i = 0; i < (unsigned)size; i++ )
                    {
                        ((short*)array)[i] = ((short*)a)[i];
                    }
                    break;
                }
                case ConstantType::INT:
                {
                    array = new int[(unsigned)size];
                    for( unsigned i = 0; i < (unsigned)size; i++ )
                    {
                        ((int*)array)[i] = ((int*)a)[i];
                    }
                    break;
                }
                case ConstantType::FLOAT:
                {
                    array = new float[(unsigned)size];
                    for( unsigned i = 0; i < (unsigned)size; i++ )
                    {
                        ((float*)array)[i] = ((float*)a)[i];
                    }
                    break;
                }
                case ConstantType::DOUBLE:
                {
                    array = new double[(unsigned)size];
                    for( unsigned i = 0; i < (unsigned)size; i++ )
                    {
                        ((double*)array)[i] = ((double*)a)[i];
                    }
                    break;
                }
                case ConstantType::INT64: 
                {
                    array = new int64_t[(unsigned)size];
                    for( unsigned i = 0; i < (unsigned)size; i++ )
                    {
                        ((int64_t*)array)[i] = ((int64_t*)a)[i];
                    }
                    break;
                }
                case ConstantType::UNKNOWN: a = nullptr; break;
            }
        }
        ~ConstantArray()
        {
            switch(t)
            {
                case ConstantType::SHORT : delete (short*)array; break;
                case ConstantType::INT   : delete (int*)array; break;
                case ConstantType::FLOAT : delete (float*)array; break;
                case ConstantType::DOUBLE: delete (double*)array; break;
                case ConstantType::INT64 : delete (int64_t*)array; break;
                case ConstantType::UNKNOWN: break;
            }
        }
        std::string dumpHalide( const std::map<std::shared_ptr<Dimension>, std::shared_ptr<ReductionVariable>>& dimToRV ) const override;
        std::string dumpC() const override;
        const std::vector<std::shared_ptr<IndexVariable>>& getVars() const;
        ConstantType getArray(void** ret) const;
        int getArraySize() const;
    private:
        /// The index variables that index the constant array, if any
        std::vector<std::shared_ptr<IndexVariable>> vars;
        /// A pointer to the array of constant values held by this constant
        void* array;
        /// The number of entries in the array
        int arraySize;
    };

    std::string getArrayType(const llvm::Constant* ptr);
    void getConstant( const std::shared_ptr<Cyclebite::Graph::Inst>& opInst, 
                      const llvm::Constant* con, 
                      std::vector<std::shared_ptr<Symbol>>& newSymbols, 
                      std::map<std::shared_ptr<Cyclebite::Graph::DataValue>, std::shared_ptr<Symbol>>& nodeToExpr );
} // namespace Cyclebite::Grammar