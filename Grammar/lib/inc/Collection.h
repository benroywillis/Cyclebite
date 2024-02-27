//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include "IndexVariable.h"
#include "llvm/IR/Instructions.h"

namespace Cyclebite::Grammar
{
    class Collection : public Symbol
    {
    public:
        /// @param v    The group of index variables that offset the memory of this collection. They should define the access patterns of the collection.
        /// @param p    The index base pointer of this collection. The index base pointer is the one whose memory is indexed by this collection.
        /// @param e    The memory primitive(s) that are produced by this collection. This represents the loaded or stored values or places this collection facilitates.
        Collection( const std::vector<std::shared_ptr<IndexVariable>>& v, const std::shared_ptr<BasePointer>& p, const std::set<const llvm::Value*>& e );
        ~Collection() = default;
        std::string getBoundedName() const;
        uint32_t getNumDims() const;
        /// Returns the index variables belonging to this collection that are dimensions of the underlying base pointer
        const std::set<std::shared_ptr<Dimension>, DimensionSort> getDimensions() const;
        /// Returns the spaces of each dimension in the collection
        std::vector<PolySpace> getPolyhedralSpace() const;
        const std::shared_ptr<IndexVariable>& operator[](unsigned i) const;
        /// @brief Returns the indexBP of the collection
        const std::shared_ptr<BasePointer>& getBP() const;
        /// @brief Returns the BPs used by the index variables of this collection
        const std::set<std::shared_ptr<BasePointer>>& getOffsetBPs() const;
        /// @brief Returns the index variables of this collection in their hierarchical order, parent-most to child-most 
        const std::vector<std::shared_ptr<IndexVariable>>& getIndices() const;
        /// @brief  Returns the instruction(s) that return an element from this collection
        /// Each collection is given the ld/st instructions that served as their starting points, and this method returns them
        /// @return Set of instructions that either load an element from or store an element to this collection
        const std::set<const llvm::Value*> getElementPointers() const;
        const llvm::LoadInst* getLoad() const;
        const std::set<const llvm::StoreInst*> getStores() const;
        /// @brief Returns the dimensions in which this collection and the input overlap
        ///
        /// Two dimensions overlap if 
        ///   1. Their integer space intersection is non-null
        ///   2. They index the same base pointer
        ///   3. There is a modifier on a dimension that implies a dependency on a previously-computed result
        /// @param coll The collection whose dimensions and base pointer should be compared to the current
        /// @return All dimensions that overlap between the two. Set entries are guaranteed to be dimensions
        std::set<std::shared_ptr<IndexVariable>> overlaps( const std::shared_ptr<Collection>& coll ) const;
        std::string dump() const override;
        std::string dumpHalide( const std::map<std::shared_ptr<Symbol>, std::shared_ptr<Symbol>>& symbol2Symbol ) const override;
    protected:
        std::vector<std::shared_ptr<IndexVariable>> vars;
        /// indexBP is the base pointer this collection loads from
        std::shared_ptr<BasePointer> indexBP;
        /// offsetBPs are base pointers used by the indexVariables of this collection
        std::set<std::shared_ptr<BasePointer>> offsetBPs;
    private:
        /// Element pointers
        /// Each collection is constructed from a load or store (or both) 
        /// This set remembers those instructions for later reference (when expressions are being built and we have to find a collection that explains a loaded or stored value)
       std::set<const llvm::Value*> eps;
    };
    std::set<std::shared_ptr<Collection>> getCollections(const std::shared_ptr<Task>& t, const std::set<std::shared_ptr<BasePointer>>& bps, const std::set<std::shared_ptr<IndexVariable>>& idxVars);
} // namespace Cyclebite::Grammar