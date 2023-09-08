#include "Annotate.h"
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
} // namespace Cyclebite::Profile::Passes