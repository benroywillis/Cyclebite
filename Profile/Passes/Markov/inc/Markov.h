//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
/*#include <llvm/Pass.h>

namespace Cyclebite::Profile::Passes
{
    struct Markov : public FunctionPass
    {
        static char ID;
        Markov() : FunctionPass(ID) {}
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
    struct Markov : llvm::PassInfoMixin<Markov> 
    {
        llvm::PreservedAnalyses run(llvm::Module& M, llvm::ModuleAnalysisManager& );
        // without setting this to true, all modules with "optnone" attribute are skipped
        static bool isRequired() { return true; }
    };
} // namespace Cyclebite::Profile::Passes