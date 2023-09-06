#pragma once
#include <llvm/Pass.h>

using namespace llvm;

namespace Cyclebite::Profile::Passes
{
    struct TraceIO : public ModulePass
    {
        static char ID;
        TraceIO() : ModulePass(ID) {}
        bool runOnModule(Module &M) override;
        void getAnalysisUsage(AnalysisUsage &AU) const override;
        bool doInitialization(Module &M) override;
    };
} // namespace Cyclebite::Profile::Passes
