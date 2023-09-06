#pragma once
#include <llvm/IR/BasicBlock.h>
#include <llvm/Pass.h>

using namespace llvm;

namespace Cyclebite::Profile::Passes
{
    struct PapiExport : public FunctionPass
    {
        static char ID;
        PapiExport() : FunctionPass(ID) {}
        bool runOnFunction(Function &F) override;
        bool doInitialization(Module &M) override;
        void getAnalysisUsage(AnalysisUsage &AU) const override;
    };
} // namespace Cyclebite::Profile::Passes
