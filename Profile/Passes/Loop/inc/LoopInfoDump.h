#pragma once
#include <llvm/Pass.h>

using namespace llvm;

#pragma clang diagnostic ignored "-Woverloaded-virtual"

namespace DashTracer
{
    namespace Passes
    {
        struct LoopInfoDump : public ModulePass
        {
            static char ID;
            LoopInfoDump() : ModulePass(ID) {}
            bool runOnModule(Module &M) override;
            void getAnalysisUsage(AnalysisUsage &AU) const override;
        };
    } // namespace Passes
} // namespace DashTracer
