#include "LastWriter.h"
#include "Annotate.h"
#include "Util/Annotate.h"
#include "CommandArgs.h"
#include "Functions.h"
#include "llvm/IR/DataLayout.h"
#include <fstream>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/OperandTraits.h>
#include <llvm/IR/Type.h>
#include <llvm/Pass.h>
#include <llvm/Support/raw_ostream.h>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

using namespace llvm;
using namespace std;

namespace DashTracer::Passes
{
    bool LastWriter::runOnFunction(Function &F)
    {
        for (auto BB = F.begin(); BB != F.end(); BB++)
        {
            auto *block = cast<BasicBlock>(BB);
            auto dl = block->getModule()->getDataLayout();
            int64_t blockId = GetBlockID(block);
            auto firstInsertion = BB->getFirstInsertionPt();
            auto *firstInst = cast<Instruction>(firstInsertion);
            IRBuilder<> firstBuilder(firstInst);

            // skip this if we are in the first block of main
            if (!((F.getName() == "main") && (BB == F.begin())))
            {
                Value *idValue = ConstantInt::get(Type::getInt64Ty(BB->getContext()), (uint64_t)blockId);
                std::vector<Value *> args;
                args.push_back(idValue);
                auto call = firstBuilder.CreateCall(LastWriterIncrement, args);
                call->setDebugLoc(NULL);
            }

            if (F.getName() == "main")
            {
                if (BB == F.begin())
                {
                    IRBuilder<> initBuilder(firstInst);
                    std::vector<Value *> values;
                    Value *blockID = ConstantInt::get(Type::getInt64Ty(block->getContext()), (uint64_t)blockId);
                    values.push_back(blockID);
                    auto call = initBuilder.CreateCall(LastWriterInitialization, values);
                    call->setDebugLoc(NULL);
                }
                else if (auto retInst = dyn_cast<ReturnInst>(BB->getTerminator()))
                {
                    auto endInsertion = BB->getTerminator();
                    auto *lastInst = cast<Instruction>(endInsertion);
                    IRBuilder<> lastBuilder(lastInst);
                    auto call = lastBuilder.CreateCall(LastWriterDestroy);
                    call->setDebugLoc(NULL);
                }
                else if (auto resumeInst = dyn_cast<ResumeInst>(BB->getTerminator()))
                {
                    auto endInsertion = BB->getTerminator();
                    auto *lastInst = cast<Instruction>(endInsertion);
                    IRBuilder<> lastBuilder(lastInst);
                    auto call = lastBuilder.CreateCall(LastWriterDestroy);
                    call->setDebugLoc(NULL);
                }
                else if (auto unreachableInst = dyn_cast<UnreachableInst>(BB->getTerminator()))
                {
                    auto endInsertion = BB->getTerminator();
                    auto *lastInst = cast<Instruction>(endInsertion);
                    IRBuilder<> lastBuilder(lastInst);
                    auto call = lastBuilder.CreateCall(LastWriterDestroy);
                    call->setDebugLoc(NULL);
                }
            }
            for (auto bi = BB->begin(); bi != BB->end(); bi++)
            {
                if (auto call = dyn_cast<CallBase>(bi))
                {
                    if (call->getCalledFunction())
                    {
                        if (call->getCalledFunction()->getName() == "exit")
                        {
                            IRBuilder<> destroyInserter(call);
                            auto insert = destroyInserter.CreateCall(LastWriterDestroy);
                            insert->moveBefore(call);
                            call->setDebugLoc(NULL);
                        }
                    }
                }
            }

            uint32_t ldInstructionIndex = 0;
            uint32_t stInstructionIndex = 0;
            for (BasicBlock::iterator BI = block->begin(), BE = block->end(); BI != BE; ++BI)
            {
                auto *CI = dyn_cast<Instruction>(BI);
                std::vector<Value *> values;
                if (auto *load = dyn_cast<LoadInst>(CI))
                {
                    IRBuilder<> builder(load);
                    Value *addr = load->getPointerOperand();
                    auto *type = load->getType();
                    uint64_t dataSize = dl.getTypeAllocSize(type);
                    // addr
                    auto castCode = CastInst::getCastOpcode(addr, true, PointerType::get(Type::getInt8PtrTy(block->getContext()), 0), true);
                    Value *addrCast = builder.CreateCast(castCode, addr, Type::getInt8PtrTy(block->getContext()));
                    values.push_back(addrCast);
                    //bb id
                    Value *blockID = ConstantInt::get(Type::getInt64Ty(block->getContext()), (uint64_t)blockId);
                    values.push_back(blockID);
                    // instruction index
                    Value *instructionID = ConstantInt::get(Type::getInt32Ty(block->getContext()), (uint32_t)ldInstructionIndex++);
                    values.push_back(instructionID);
                    // data size
                    Value *dataSizeValue = ConstantInt::get(Type::getInt64Ty(block->getContext()), dataSize);
                    values.push_back(dataSizeValue);
                    auto call = builder.CreateCall(LastWriterLoad, values);
                    call->setDebugLoc(NULL);
                }
                else if (auto *store = dyn_cast<StoreInst>(CI))
                {
                    IRBuilder<> builder(store);
                    Value *addr = store->getPointerOperand();
                    auto *type = store->getValueOperand()->getType();
                    uint64_t dataSize = dl.getTypeAllocSize(type);
                    // addr
                    auto castCode = CastInst::getCastOpcode(addr, true, PointerType::get(Type::getInt8PtrTy(block->getContext()), 0), true);
                    Value *addrCast = builder.CreateCast(castCode, addr, Type::getInt8PtrTy(block->getContext()));
                    values.push_back(addrCast);
                    //bb id
                    Value *blockID = ConstantInt::get(Type::getInt64Ty(block->getContext()), (uint64_t)blockId);
                    values.push_back(blockID);
                    // instruction index
                    Value *instructionID = ConstantInt::get(Type::getInt32Ty(block->getContext()), (uint32_t)stInstructionIndex++);
                    values.push_back(instructionID);
                    // data size
                    Value *dataSizeValue = ConstantInt::get(Type::getInt64Ty(block->getContext()), dataSize);
                    values.push_back(dataSizeValue);
                    auto call = builder.CreateCall(LastWriterStore, values);
                    call->setDebugLoc(NULL);
                }
            }
        }
        return true;
    }

    bool LastWriter::doInitialization(Module &M)
    {
        LastWriterLoad = cast<Function>(M.getOrInsertFunction("LastWriterLoad", Type::getVoidTy(M.getContext()), Type::getIntNPtrTy(M.getContext(), 8), Type::getInt64Ty(M.getContext()), Type::getInt32Ty(M.getContext()), Type::getInt64Ty(M.getContext())).getCallee());
        LastWriterStore = cast<Function>(M.getOrInsertFunction("LastWriterStore", Type::getVoidTy(M.getContext()), Type::getIntNPtrTy(M.getContext(), 8), Type::getInt64Ty(M.getContext()), Type::getInt32Ty(M.getContext()), Type::getInt64Ty(M.getContext())).getCallee());
        LastWriterIncrement = cast<Function>(M.getOrInsertFunction("LastWriterIncrement", Type::getVoidTy(M.getContext()), Type::getInt64Ty(M.getContext())).getCallee());
        LastWriterInitialization = cast<Function>(M.getOrInsertFunction("LastWriterInitialization", Type::getVoidTy(M.getContext()), Type::getInt64Ty(M.getContext())).getCallee());
        LastWriterDestroy = cast<Function>(M.getOrInsertFunction("LastWriterDestroy", Type::getVoidTy(M.getContext())).getCallee());
        return false;
    }

    void LastWriter::getAnalysisUsage(AnalysisUsage &AU) const
    {
        AU.addRequired<DashTracer::Passes::EncodedAnnotate>();
    }
    char LastWriter::ID = 1;
    static RegisterPass<LastWriter> Y("LastWriter", "memory profiler", true, false);
} // namespace DashTracer::Passes