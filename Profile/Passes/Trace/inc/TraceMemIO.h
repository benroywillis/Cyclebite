#pragma once
#include <llvm/Pass.h>
using namespace llvm;

namespace Cyclebite::Profile::Passes
{
    struct TraceMemIO : public ModulePass
    {
        static char ID;
        TraceMemIO() : ModulePass(ID) {}
        bool runOnModule(Module &M) override;
        void getAnalysisUsage(AnalysisUsage &AU) const override;
        bool doInitialization(Module &M) override;
    };
} // namespace Cyclebite::Profile::Passes
