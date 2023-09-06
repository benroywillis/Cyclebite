
#include "MarkovIO.h"
#include "Annotate.h"
#include "Util/Annotate.h"
#include "Util/Format.h"
#include "CommandArgs.h"
#include "Functions.h"
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/Pass.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils/ModuleUtils.h>

using namespace llvm;

namespace Cyclebite::Profile::Passes
{
    GlobalVariable *tst;
    bool MarkovIO::runOnModule(Module &M)
    {
        uint64_t blockCount = GetBlockCount(&M);
        ConstantInt *i = ConstantInt::get(Type::getInt64Ty(M.getContext()), blockCount);
        tst = new GlobalVariable(M, i->getType(), false, llvm::GlobalValue::LinkageTypes::ExternalLinkage, i, "MarkovBlockCount");
        return true;
    }

    void MarkovIO::getAnalysisUsage(AnalysisUsage &AU) const
    {
        AU.setPreservesAll();
    }
    char MarkovIO::ID = 0;
    static RegisterPass<MarkovIO> MarkovIO("MarkovIO", "Adds function calls to open/close markov files", true, false);
} // namespace Cyclebite::Profile
