#pragma once
#include <llvm/Pass.h>

using namespace llvm;

#pragma clang diagnostic ignored "-Woverloaded-virtual"

namespace Cyclebite::Profile::Passes
{
    struct EncodedTrace : public FunctionPass
    {
        static char ID;
        EncodedTrace() : FunctionPass(ID) {}
        bool runOnFunction(Function &F) override;
        void getAnalysisUsage(AnalysisUsage &AU) const override;
        bool doInitialization(Module &M) override;
    };
} // namespace Cyclebite::Profile::Passes
