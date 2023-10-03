/*#include "Annotate.h"
#include "Util/Format.h"
#include <iostream>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Type.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>

using namespace llvm;

namespace Cyclebite::Profile::Passes
{
    // legacy pass manager
    cl::opt<uint64_t> CyclebiteStartIndex("tai", llvm::cl::desc("Initial block index"), llvm::cl::value_desc("Initial block index"));
    cl::opt<uint64_t> CyclebiteStartValueIndex("tavi", llvm::cl::desc("Initial value index"), llvm::cl::value_desc("Initial value index"));
    bool EncodedAnnotate::runOnModule(Module &M)
    {
        std::cout << "Starting EncodedAnnotate module run" << std::endl;
        if (CyclebiteStartIndex.getNumOccurrences() != 0)
        {
            CyclebiteIndex = CyclebiteStartIndex;
        }
        if (CyclebiteStartValueIndex.getNumOccurrences() != 0)
        {
            CyclebiteValueIndex = CyclebiteStartValueIndex;
        }
        Format(&M);
        if (CyclebiteStartIndex.getNumOccurrences() != 0)
        {
            std::cout << "Ending Cyclebite block index: " << CyclebiteIndex << std::endl;
        }
        if (CyclebiteStartValueIndex.getNumOccurrences() != 0)
        {
            std::cout << "Ending Cyclebite value index: " << CyclebiteValueIndex << std::endl;
        }
        return true;
    }

    void EncodedAnnotate::getAnalysisUsage(AnalysisUsage &AU) const
    {
        AU.setPreservesAll();
    }
    char EncodedAnnotate::ID = 0;
    static RegisterPass<EncodedAnnotate> Z("EncodedAnnotate", "Renames the basic blocks in the module", true, false);

    // new pass manager

} // namespace Cyclebite::Profile::Passes
*/

#include "inc/Annotate.h"
#include "Util/Format.h"
#include <spdlog/spdlog.h>

using namespace llvm;
using namespace Cyclebite;

PreservedAnalyses Profile::Passes::Annotate::Annotate::run(Module& M, ModuleAnalysisManager& ) 
{
    uint64_t blockCount = Util::GetBlockCount(M);
    ConstantInt *i = ConstantInt::get(Type::getInt64Ty(M.getContext()), blockCount);
    new GlobalVariable(M, i->getType(), false, llvm::GlobalValue::LinkageTypes::ExternalLinkage, i, "MarkovBlockCount");

    Util::Format(M);
    return PreservedAnalyses::none();
}

// without setting this to true, all modules with "optnone" attribute are skipped
bool Profile::Passes::Annotate::isRequired() { return true; }

// new pass manager registration
llvm::PassPluginLibraryInfo getAnnotatePluginInfo() 
{
    return {LLVM_PLUGIN_API_VERSION, "Annotate", LLVM_VERSION_STRING, 
        [](PassBuilder &PB) 
        {
            PB.registerPipelineParsingCallback( 
                [](StringRef Name, ModulePassManager& MPM, ArrayRef<PassBuilder::PipelineElement>) 
                {
                    if (Name == "Annotate") {
                        MPM.addPass(Profile::Passes::Annotate());
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
    return getAnnotatePluginInfo();
}
