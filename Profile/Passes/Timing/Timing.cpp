/*#include "Timing.h"
#include "Annotate.h"
#include "Util/Annotate.h"
#include "Functions.h"
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Type.h>
#include <llvm/Pass.h>
#include <llvm/Support/raw_ostream.h>
#include <string>
#include <vector>

using namespace llvm;


namespace Cyclebite::Profile::Passes
{
    bool Timing::runOnFunction(Function &F)
    {
        for (auto fi = F.begin(); fi != F.end(); fi++)
        {
            auto *BB = cast<BasicBlock>(fi);
            auto firstInsertion = BB->getFirstInsertionPt();
            auto *firstInst = cast<Instruction>(firstInsertion);
            IRBuilder<> firstBuilder(firstInst);

            // two things to check
            // first, see if this is the first block of main, and if it is, insert TimingInit to the first instruction of the block
            // second, see if there is a return from main here, and if it is, insert TimingDestroy and move it right before the ReturnInst
            if (F.getName() == "main")
            {
                // TimingInit
                if (fi == F.begin())
                {
                    // get blockCount and make it a value in the LLVM Module
                    firstInsertion = BB->getFirstInsertionPt();
                    firstInst = cast<Instruction>(firstInsertion);
                    IRBuilder<> initBuilder(firstInst);
                    auto call = initBuilder.CreateCall(TimingInit);
                    call->setDebugLoc(NULL);
                }
                // TimingDestroy
                // Place this before any return from main
                else if (auto retInst = dyn_cast<ReturnInst>(fi->getTerminator()))
                {
                    auto endInsertion = BB->getTerminator();
                    auto *lastInst = cast<Instruction>(endInsertion);
                    IRBuilder<> lastBuilder(lastInst);

                    auto call = lastBuilder.CreateCall(TimingDestroy);
                    call->moveBefore(lastInst);
                    call->setDebugLoc(NULL);
                }
                else if (auto resumeInst = dyn_cast<ResumeInst>(fi->getTerminator()))
                {
                    auto endInsertion = BB->getTerminator();
                    auto *lastInst = cast<Instruction>(endInsertion);
                    IRBuilder<> lastBuilder(lastInst);

                    auto call = lastBuilder.CreateCall(TimingDestroy);
                    call->moveBefore(lastInst);
                    call->setDebugLoc(NULL);
                }
                else if (auto unreachableInst = dyn_cast<UnreachableInst>(fi->getTerminator()))
                {
                    auto endInsertion = BB->getTerminator();
                    auto *lastInst = cast<Instruction>(endInsertion);
                    IRBuilder<> lastBuilder(lastInst);

                    auto call = lastBuilder.CreateCall(TimingDestroy);
                    call->moveBefore(lastInst);
                    call->setDebugLoc(NULL);
                }
            }
            // we also have to look for the exit() function from libc
            for (auto bi = fi->begin(); bi != fi->end(); bi++)
            {
                if (auto call = dyn_cast<CallBase>(bi))
                {
                    if (call->getCalledFunction())
                    {
                        if (call->getCalledFunction()->getName() == "exit")
                        {
                            IRBuilder<> destroyInserter(call);
                            auto insert = destroyInserter.CreateCall(TimingDestroy);
                            insert->moveBefore(call);
                            call->setDebugLoc(NULL);
                        }
                    }
                }
            }
        }
        return true;
    }

    bool Timing::doInitialization(Module &M)
    {
        TimingInit = cast<Function>(M.getOrInsertFunction("TimingInit", Type::getVoidTy(M.getContext())).getCallee());
        TimingDestroy = cast<Function>(M.getOrInsertFunction("TimingDestroy", Type::getVoidTy(M.getContext())).getCallee());
        return false;
    }

    void Timing::getAnalysisUsage(AnalysisUsage &AU) const
    {
        AU.addRequired<Cyclebite::Profile::Passes::Annotate>();
    }

    char Timing::ID = 0;
    static RegisterPass<Timing> Y("Timing", "Adds Timing Dumping to the binary", true, false);
} // namespace Cyclebite::Profile::Passes
*/

#include "inc/Timing.h"
#include "../Utilities/inc/Functions.h"
#include <llvm/IR/IRBuilder.h>
#include <spdlog/spdlog.h>

using namespace llvm;

llvm::PreservedAnalyses Cyclebite::Profile::Passes::Timing::run(llvm::Module& M, llvm::ModuleAnalysisManager& )
{
    TimingInit = cast<Function>(M.getOrInsertFunction("TimingInit", Type::getVoidTy(M.getContext())).getCallee());
    TimingDestroy = cast<Function>(M.getOrInsertFunction("TimingDestroy", Type::getVoidTy(M.getContext())).getCallee());
    for( auto& F : M )
    {
        for (auto fi = F.begin(); fi != F.end(); fi++)
        {
            auto *BB = cast<BasicBlock>(fi);
            auto firstInsertion = BB->getFirstInsertionPt();
            auto *firstInst = cast<Instruction>(firstInsertion);
            IRBuilder<> firstBuilder(firstInst);

            // two things to check
            // first, see if this is the first block of main, and if it is, insert TimingInit to the first instruction of the block
            // second, see if there is a return from main here, and if it is, insert TimingDestroy and move it right before the ReturnInst
            if (F.getName() == "main")
            {
                // TimingInit
                if (fi == F.begin())
                {
                    // get blockCount and make it a value in the LLVM Module
                    firstInsertion = BB->getFirstInsertionPt();
                    firstInst = cast<Instruction>(firstInsertion);
                    IRBuilder<> initBuilder(firstInst);
                    auto call = initBuilder.CreateCall(TimingInit);
                    call->setDebugLoc(NULL);
                }
                // TimingDestroy
                // Place this before any return from main
                else if (auto retInst = dyn_cast<ReturnInst>(fi->getTerminator()))
                {
                    auto endInsertion = BB->getTerminator();
                    auto *lastInst = cast<Instruction>(endInsertion);
                    IRBuilder<> lastBuilder(lastInst);

                    auto call = lastBuilder.CreateCall(TimingDestroy);
                    call->moveBefore(lastInst);
                    call->setDebugLoc(NULL);
                }
                else if (auto resumeInst = dyn_cast<ResumeInst>(fi->getTerminator()))
                {
                    auto endInsertion = BB->getTerminator();
                    auto *lastInst = cast<Instruction>(endInsertion);
                    IRBuilder<> lastBuilder(lastInst);

                    auto call = lastBuilder.CreateCall(TimingDestroy);
                    call->moveBefore(lastInst);
                    call->setDebugLoc(NULL);
                }
                else if (auto unreachableInst = dyn_cast<UnreachableInst>(fi->getTerminator()))
                {
                    auto endInsertion = BB->getTerminator();
                    auto *lastInst = cast<Instruction>(endInsertion);
                    IRBuilder<> lastBuilder(lastInst);

                    auto call = lastBuilder.CreateCall(TimingDestroy);
                    call->moveBefore(lastInst);
                    call->setDebugLoc(NULL);
                }
            }
            // we also have to look for the exit() function from libc
            for (auto bi = fi->begin(); bi != fi->end(); bi++)
            {
                if (auto call = dyn_cast<CallBase>(bi))
                {
                    if (call->getCalledFunction())
                    {
                        if (call->getCalledFunction()->getName() == "exit")
                        {
                            IRBuilder<> destroyInserter(call);
                            auto insert = destroyInserter.CreateCall(TimingDestroy);
                            insert->moveBefore(call);
                            call->setDebugLoc(NULL);
                        }
                    }
                }
            }
        }
    }
    return PreservedAnalyses::none();
}

// new pass manager registration
PassPluginLibraryInfo getTimingPluginInfo() 
{
    return {LLVM_PLUGIN_API_VERSION, "Timing", LLVM_VERSION_STRING, 
        [](PassBuilder &PB) 
        {
            PB.registerPipelineParsingCallback( 
                [](StringRef Name, ModulePassManager& FPM, ArrayRef<PassBuilder::PipelineElement>) 
                {
                    if (Name == "Timing") {
                        FPM.addPass(Cyclebite::Profile::Passes::Timing());
                        return true;
                    }
                    return false;
                }
            );
        }
    };
}

// guarantees this pass will be visible to opt when called
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() 
{
    return getTimingPluginInfo();
}