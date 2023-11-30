//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
/*#include <llvm/Pass.h>

namespace Cyclebite::Profile::Passes
{
    struct Precision : public FunctionPass
    {
        static char ID;
        Precision() : FunctionPass(ID) {}
        bool runOnFunction(Function &F) override;
        void getAnalysisUsage(AnalysisUsage &AU) const override;
        bool doInitialization(Module &M) override;
    };
} // namespace Cyclebite
*/
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

namespace Cyclebite::Profile::Passes
{
    struct Precision : llvm::PassInfoMixin<Precision> 
    {
        llvm::PreservedAnalyses run(llvm::Function& M, llvm::FunctionAnalysisManager& );
        // without setting this to true, all modules with "optnone" attribute are skipped
        static bool isRequired();
    };
} // namespace Cyclebite::Profile::Passes