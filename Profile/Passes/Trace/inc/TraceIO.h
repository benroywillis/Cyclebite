#pragma once
#include <llvm/Pass.h>
using namespace llvm;

namespace DashTracer
{
    namespace Passes
    {
        /// <summary>
        /// The TraceIO pass inserts file IO instructions to the source bitcode.
        /// </summary>
        struct TraceIO : public ModulePass
        {
            static char ID;
            TraceIO() : ModulePass(ID) {}
            bool runOnModule(Module &M) override;
            void getAnalysisUsage(AnalysisUsage &AU) const override;
            bool doInitialization(Module &M) override;
        };
    } // namespace Passes
} // namespace DashTracer
