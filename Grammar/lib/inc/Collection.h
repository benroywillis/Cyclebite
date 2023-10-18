//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include "IndexVariable.h"
#include "Symbol.h"
#include "llvm/IR/Instructions.h"

namespace Cyclebite::Grammar
{
    class Collection : public Symbol
    {
    public:
        /// @param v    The group of index variables that offset the memory of this collection. They should define the access patterns of the collection.
        /// @param p    The base pointers of this collection. Multiple base pointers can be used - e.g., one to offset with index variables, and one that is used by the index variable(s) themselves
        /// @param e    The memory primitive(s) that are produced by this collection. This represents the loaded or stored values or places this collection facilitates.
        Collection( const std::vector<std::shared_ptr<IndexVariable>>& v, const std::set<std::shared_ptr<BasePointer>>& p, const std::set<const llvm::Value*>& e );
        ~Collection() = default;
        uint32_t getNumDims() const;
        const std::shared_ptr<IndexVariable>& operator[](unsigned i) const;
        const std::set<std::shared_ptr<BasePointer>>& getBPs() const;
        const std::vector<std::shared_ptr<IndexVariable>>& getIndices() const;
        /// @brief  Returns the instruction(s) that return an element from this collection
        /// Each collection is given the ld/st instructions that served as their starting points, and this method returns them
        /// @return Set of instructions that either load an element from or store an element to this collection
        const std::set<const llvm::Value*> getElementPointers() const;
        const llvm::LoadInst* getLoad() const;
        const std::set<const llvm::StoreInst*> getStores() const;
        std::string dump() const override;
    protected:
        std::vector<std::shared_ptr<IndexVariable>> vars;
        std::set<std::shared_ptr<BasePointer>> bps;
    private:
        /// Element pointers
        /// Each collection is constructed from a load or store (or both) 
        /// This set remembers those instructions for later reference (when expressions are being built and we have to find a collection that explains a loaded or stored value)
       std::set<const llvm::Value*> eps;
    };
    std::set<std::shared_ptr<Collection>> getCollections(const std::shared_ptr<Task>& t, const std::set<std::shared_ptr<IndexVariable>>& idxVars);
} // namespace Cyclebite::Grammar