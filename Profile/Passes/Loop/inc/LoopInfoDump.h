#pragma once
#include <llvm/Pass.h>

using namespace llvm;

#pragma clang diagnostic ignored "-Woverloaded-virtual"

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
