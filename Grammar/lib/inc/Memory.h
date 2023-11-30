//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include <llvm/IR/Instructions.h>
#include <vector>

namespace Cyclebite::Grammar
{
    // geps form trees with multiple children
    // gep<ptr, idx0, idx1, ...>
    // child geps can come from the ptr and all its indices
    // to understand how the geps are working together, we build a tree of them, then map each gep to its idxVars
    struct GepNode
    {
        const llvm::GetElementPtrInst* node;
        const llvm::GetElementPtrInst* ptrGep;
        std::vector<const llvm::GetElementPtrInst*> idxGeps;
    };
    struct GepTreeSort
    {
        using is_transparent = void;
        bool operator()( const GepNode& lhs, const GepNode& rhs ) const
        {
            return lhs.node < rhs.node;
        }
        bool operator()( const GepNode& lhs, const llvm::GetElementPtrInst* rhs ) const
        {
            return lhs.node < rhs;
        }
        bool operator()( const llvm::GetElementPtrInst* lhs, const GepNode& rhs ) const
        {
            return lhs < rhs.node;
        }
    };
    std::set<GepNode, GepTreeSort> buildGepTree(const std::shared_ptr<Task>& t);
} // namespace Cyclebite::Grammar