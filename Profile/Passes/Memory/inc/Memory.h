//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
/*#include <llvm/Pass.h>

namespace Cyclebite::Profile::Passes
{
    struct Memory : public FunctionPass
    {
        static char ID;
        Memory() : FunctionPass(ID) {}
        bool runOnFunction(Function &F) override;
        void getAnalysisUsage(AnalysisUsage &AU) const override;
        bool doInitialization(Module &M) override;
    };
} // namespace Cyclebite::Profile::Passes
*/
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

namespace Cyclebite::Profile::Passes
{
    /// Minimum offset a memory tuple must have (in bytes) to be considered a base pointer
    constexpr uint32_t MIN_MEMORY_OFFSET = 128;
    struct Memory : llvm::PassInfoMixin<Memory> 
    {
        llvm::PreservedAnalyses run(llvm::Module& M, llvm::ModuleAnalysisManager& );
        // without setting this to true, all modules with "optnone" attribute are skipped
        static bool isRequired() { return true; }
    };
} // namespace Cyclebite::Profile::Passes