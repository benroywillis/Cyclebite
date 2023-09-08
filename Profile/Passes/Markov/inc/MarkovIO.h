#pragma once
#include <llvm/Pass.h>
using namespace llvm;

namespace Cyclebite::Profile::Passes
{
    struct MarkovIO : public ModulePass
    {
        static char ID;
        MarkovIO() : ModulePass(ID) {}
        bool runOnModule(Module &M) override;
        void getAnalysisUsage(AnalysisUsage &AU) const override;
    };
} // namespace Cyclebite::Profile::Passes
