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
    struct Memory : llvm::PassInfoMixin<Memory> 
    {
        llvm::PreservedAnalyses run(llvm::Function& M, llvm::FunctionAnalysisManager& );
        // without setting this to true, all modules with "optnone" attribute are skipped
        static bool isRequired() { return true; }
    };
} // namespace Cyclebite::Profile::Passes