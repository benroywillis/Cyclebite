#pragma once
/*#include <llvm/Pass.h>

namespace Cyclebite::Profile::Passes
{
    struct LoopInfoDump : public ModulePass
    {
        static char ID;
        LoopInfoDump() : ModulePass(ID) {}
        bool runOnModule(Module &M) override;
        void getAnalysisUsage(AnalysisUsage &AU) const override;
    };
} // namespace Cyclebite::Profile::Passes
*/
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

namespace Cyclebite::Profile::Passes
{
    struct LoopInfoDump : llvm::PassInfoMixin<LoopInfoDump> 
    {
        llvm::PreservedAnalyses run(llvm::Module& M, llvm::ModuleAnalysisManager& );
        // without setting this to true, all modules with "optnone" attribute are skipped
        static bool isRequired();
    };
} // namespace Cyclebite::Profile::Passes