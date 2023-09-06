#pragma once
#include <llvm/IR/Function.h>
#include <llvm/Pass.h>

using namespace llvm;

namespace Cyclebite::Profile::Passes
{
    struct PapiIO : public ModulePass
    {
        static char ID;
        PapiIO() : ModulePass(ID) {}
        bool runOnModule(Module &M) override;
        void getAnalysisUsage(AnalysisUsage &AU) const override;
        bool doInitialization(Module &M) override;
    };
} // namespace Cyclebite::Profile::Passes
