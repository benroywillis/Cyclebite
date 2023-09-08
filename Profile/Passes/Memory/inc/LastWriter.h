#pragma once
#include <llvm/IR/BasicBlock.h>
#include <llvm/Pass.h>

using namespace llvm;

#pragma clang diagnostic ignored "-Woverloaded-virtual"

namespace Cyclebite::Profile::Passes
{
    struct LastWriter : public FunctionPass
    {
        static char ID;
        LastWriter() : FunctionPass(ID) {}
        bool runOnFunction(Function &F) override;
        void getAnalysisUsage(AnalysisUsage &AU) const override;
        bool doInitialization(Module &M) override;
    };
} // namespace Cyclebite::Profile::Passes
