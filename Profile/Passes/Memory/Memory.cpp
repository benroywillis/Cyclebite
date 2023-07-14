#include "Memory.h"
#include "Annotate.h"
#include "AtlasUtil/Annotate.h"
#include "CommandArgs.h"
#include "Functions.h"
#include "MarkovIO.h"
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
#include <deque>

using namespace llvm;
using namespace std;

namespace DashTracer::Passes
{
    bool Memory::runOnFunction(Function &F)
    {
        auto dl = F.getParent()->getDataLayout();
        for (auto fi = F.begin(); fi != F.end(); fi++)
        {
            auto BB = cast<BasicBlock>(fi);
            int64_t blockId = GetBlockID(BB);
            auto firstInsertion = BB->getFirstInsertionPt();
            auto *firstInst = cast<Instruction>(firstInsertion);
            IRBuilder<> firstBuilder(firstInst);

            // skip this if we are in the first block of main
            if (!((F.getName() == "main") && (fi == F.begin())))
            {
                Value *idValue = ConstantInt::get(Type::getInt64Ty(BB->getContext()), (uint64_t)blockId);
                std::vector<Value *> args;
                args.push_back(idValue);
                auto call = firstBuilder.CreateCall(MemoryIncrement, args);
                call->setDebugLoc(NULL);
            }

            // two things to check
            // first, see if this is the first block of main, and if it is, insert MemoryInit to the first instruction of the block
            // second, see if there is a return from main here, and if it is, insert MemoryDestroy and move it right before the ReturnInst
            if (F.getName() == "main")
            {
                // MemoryInit
                if (fi == F.begin())
                {
                    std::vector<Value *> args;
                    // get blockCount and make it a value in the LLVM Module
                    firstInsertion = BB->getFirstInsertionPt();
                    firstInst = cast<Instruction>(firstInsertion);
                    IRBuilder<> initBuilder(firstInst);
                    // get the BBID and make it a value in the LLVM Module
                    Value *blockID = ConstantInt::get(Type::getInt64Ty(BB->getContext()), (uint64_t)blockId);
                    args.push_back(blockID);
                    auto call = initBuilder.CreateCall(MemoryInit, args);
                    call->setDebugLoc(NULL);
                }
                // MemoryDestroy
                // Place this before any return from main
                else if (auto retInst = dyn_cast<ReturnInst>(fi->getTerminator()))
                {
                    auto endInsertion = BB->getTerminator();
                    auto *lastInst = cast<Instruction>(endInsertion);
                    IRBuilder<> lastBuilder(lastInst);

                    auto call = lastBuilder.CreateCall(MemoryDestroy);
                    call->moveBefore(lastInst);
                    call->setDebugLoc(NULL);
                }
                else if (auto resumeInst = dyn_cast<ResumeInst>(fi->getTerminator()))
                {
                    auto endInsertion = BB->getTerminator();
                    auto *lastInst = cast<Instruction>(endInsertion);
                    IRBuilder<> lastBuilder(lastInst);

                    auto call = lastBuilder.CreateCall(MemoryDestroy);
                    call->moveBefore(lastInst);
                    call->setDebugLoc(NULL);
                }
                else if (auto unreachableInst = dyn_cast<UnreachableInst>(fi->getTerminator()))
                {
                    auto endInsertion = BB->getTerminator();
                    auto *lastInst = cast<Instruction>(endInsertion);
                    IRBuilder<> lastBuilder(lastInst);

                    auto call = lastBuilder.CreateCall(MemoryDestroy);
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
                            auto insert = destroyInserter.CreateCall(MemoryDestroy);
                            insert->moveBefore(call);
                            call->setDebugLoc(NULL);
                        }
                    }
                }
            }
            // now inject load and store profiling operations
            uint32_t ldInstructionIndex = 0;
            uint32_t stInstructionIndex = 0;
            for (BasicBlock::iterator BI = fi->begin(), BE = fi->end(); BI != BE; ++BI)
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
                    auto castCode = CastInst::getCastOpcode(addr, true, PointerType::get(Type::getInt8PtrTy(fi->getContext()), 0), true);
                    Value *addrCast = builder.CreateCast(castCode, addr, Type::getInt8PtrTy(fi->getContext()));
                    values.push_back(addrCast);
                    //bb id
                    Value *blockID = ConstantInt::get(Type::getInt64Ty(fi->getContext()), (uint64_t)blockId);
                    values.push_back(blockID);
                    // instruction index
                    Value *instructionID = ConstantInt::get(Type::getInt32Ty(fi->getContext()), (uint32_t)ldInstructionIndex++);
                    values.push_back(instructionID);
                    // data size
                    Value *dataSizeValue = ConstantInt::get(Type::getInt64Ty(fi->getContext()), dataSize);
                    values.push_back(dataSizeValue);
                    auto call = builder.CreateCall(MemoryLoad, values);
                    call->setDebugLoc(NULL);
                }
                else if (auto *store = dyn_cast<StoreInst>(CI))
                {
                    IRBuilder<> builder(store);
                    Value *addr = store->getPointerOperand();
                    auto *type = store->getValueOperand()->getType();
                    uint64_t dataSize = dl.getTypeAllocSize(type);
                    // addr
                    auto castCode = CastInst::getCastOpcode(addr, true, PointerType::get(Type::getInt8PtrTy(fi->getContext()), 0), true);
                    Value *addrCast = builder.CreateCast(castCode, addr, Type::getInt8PtrTy(fi->getContext()));
                    values.push_back(addrCast);
                    //bb id
                    Value *blockID = ConstantInt::get(Type::getInt64Ty(fi->getContext()), (uint64_t)blockId);
                    values.push_back(blockID);
                    // instruction index
                    Value *instructionID = ConstantInt::get(Type::getInt32Ty(fi->getContext()), (uint32_t)stInstructionIndex++);
                    values.push_back(instructionID);
                    // data size
                    Value *dataSizeValue = ConstantInt::get(Type::getInt64Ty(fi->getContext()), dataSize);
                    values.push_back(dataSizeValue);
                    auto call = builder.CreateCall(MemoryStore, values);
                    call->setDebugLoc(NULL);
                }
            }
            // now inject MemoryMove, MemoryCpy, MemorySet, MemoryMalloc, MemoryFree
            for (BasicBlock::iterator BI = fi->begin(), BE = fi->end(); BI != BE; ++BI)
            {
                if( auto cpy  = llvm::dyn_cast<AnyMemCpyInst>(BI) )
                {
                    IRBuilder<> builder(cpy);
                    std::vector<Value *> values;
                    // src pointer
                    auto ptr_src = cpy->getOperand(0);
                    auto castCode = CastInst::getCastOpcode(ptr_src, true, PointerType::get(Type::getInt8PtrTy(fi->getContext()), 0), true);
                    Value *ptr_src_cast = builder.CreateCast(castCode, ptr_src, Type::getInt8PtrTy(fi->getContext()));
                    values.push_back(ptr_src_cast);
                    // snk pointer
                    auto ptr_snk = cpy->getOperand(1);
                    castCode = CastInst::getCastOpcode(ptr_snk, true, PointerType::get(Type::getInt8PtrTy(fi->getContext()), 0), true);
                    Value *ptr_snk_cast = builder.CreateCast(castCode, ptr_snk, Type::getInt8PtrTy(fi->getContext()));
                    values.push_back(ptr_snk_cast);
                    // size of the copy
                    auto size = cpy->getOperand(2);
                    castCode = CastInst::getCastOpcode(size, true, PointerType::get(Type::getInt8PtrTy(fi->getContext()), 0), true);
                    Value *size_cast = builder.CreateCast(castCode, size, Type::getInt64Ty(fi->getContext()));
                    values.push_back(size_cast);
                    // generate backend call
                    auto backendCall = builder.CreateCall(MemoryCpy, values);
                    backendCall->moveBefore(cpy);
                    backendCall->setDebugLoc(NULL);
                }
                else if( auto mov = llvm::dyn_cast<AnyMemMoveInst>(BI) )
                {
                    IRBuilder<> builder(mov);
                    std::vector<Value *> values;
                    // src pointer
                    auto ptr_src = mov->getOperand(0);
                    auto castCode = CastInst::getCastOpcode(ptr_src, true, PointerType::get(Type::getInt8PtrTy(fi->getContext()), 0), true);
                    Value *ptr_src_cast = builder.CreateCast(castCode, ptr_src, Type::getInt8PtrTy(fi->getContext()));
                    values.push_back(ptr_src_cast);
                    // snk pointer
                    auto ptr_snk = mov->getOperand(1);
                    castCode = CastInst::getCastOpcode(ptr_snk, true, PointerType::get(Type::getInt8PtrTy(fi->getContext()), 0), true);
                    Value *ptr_snk_cast = builder.CreateCast(castCode, ptr_snk, Type::getInt8PtrTy(fi->getContext()));
                    values.push_back(ptr_snk_cast);
                    // size of the move
                    auto size = mov->getOperand(2);
                    castCode = CastInst::getCastOpcode(size, true, PointerType::get(Type::getInt8PtrTy(fi->getContext()), 0), true);
                    Value *size_cast = builder.CreateCast(castCode, size, Type::getInt64Ty(fi->getContext()));
                    values.push_back(size_cast);
                    // generate backend call
                    auto backendCall = builder.CreateCall(MemoryMov, values);
                    backendCall->moveBefore(mov);
                    backendCall->setDebugLoc(NULL);
                }
                else if( auto set = llvm::dyn_cast<AnyMemSetInst>(BI) )
                {
                    IRBuilder<> builder(set);
                    std::vector<Value *> values;
                    // src pointer
                    auto ptr_src = set->getOperand(0);
                    auto castCode = CastInst::getCastOpcode(ptr_src, true, PointerType::get(Type::getInt8PtrTy(fi->getContext()), 0), true);
                    Value *ptr_src_cast = builder.CreateCast(castCode, ptr_src, Type::getInt8PtrTy(fi->getContext()));
                    values.push_back(ptr_src_cast);
                    // size of the set
                    auto size = set->getOperand(2);
                    castCode = CastInst::getCastOpcode(size, true, PointerType::get(Type::getInt8PtrTy(fi->getContext()), 0), true);
                    Value *size_cast = builder.CreateCast(castCode, size, Type::getInt64Ty(fi->getContext()));
                    values.push_back(size_cast);
                    // generate backend call
                    auto backendCall = builder.CreateCall(MemorySet, values);
                    backendCall->moveBefore(set);
                    backendCall->setDebugLoc(NULL);
                }
                else if( auto call = llvm::dyn_cast<CallBase>(BI) )
                {
                    if( call->getCalledFunction() )
                    {
                        // libc memcpy
                        if( call->getCalledFunction()->getName() == "memcpy" )
                        {
                            IRBuilder<> builder(call);
                            std::vector<Value *> values;
                            // src pointer
                            auto ptr_src = call->getOperand(1);
                            auto castCode = CastInst::getCastOpcode(ptr_src, true, PointerType::get(Type::getInt8PtrTy(fi->getContext()), 0), true);
                            Value *ptr_src_cast = builder.CreateCast(castCode, ptr_src, Type::getInt8PtrTy(fi->getContext()));
                            values.push_back(ptr_src_cast);
                            // snk pointer
                            auto ptr_snk = call->getOperand(0);
                            castCode = CastInst::getCastOpcode(ptr_snk, true, PointerType::get(Type::getInt8PtrTy(fi->getContext()), 0), true);
                            Value *ptr_snk_cast = builder.CreateCast(castCode, ptr_snk, Type::getInt8PtrTy(fi->getContext()));
                            values.push_back(ptr_snk_cast);
                            // size of the copy
                            auto size = call->getOperand(2);
                            castCode = CastInst::getCastOpcode(size, true, PointerType::get(Type::getInt8PtrTy(fi->getContext()), 0), true);
                            Value *size_cast = builder.CreateCast(castCode, size, Type::getInt64Ty(fi->getContext()));
                            values.push_back(size_cast);
                            // generate backend call
                            auto backendCall = builder.CreateCall(MemoryCpy, values);
                            backendCall->moveBefore(call);
                            backendCall->setDebugLoc(NULL);
                        }
                        // libc memmov
                        else if( call->getCalledFunction()->getName() == "memmove" )
                        {
                            IRBuilder<> builder(call);
                            std::vector<Value *> values;
                            // src pointer
                            auto ptr_src = call->getOperand(1);
                            auto castCode = CastInst::getCastOpcode(ptr_src, true, PointerType::get(Type::getInt8PtrTy(fi->getContext()), 0), true);
                            Value *ptr_src_cast = builder.CreateCast(castCode, ptr_src, Type::getInt8PtrTy(fi->getContext()));
                            values.push_back(ptr_src_cast);
                            // snk pointer
                            auto ptr_snk = call->getOperand(0);
                            castCode = CastInst::getCastOpcode(ptr_snk, true, PointerType::get(Type::getInt8PtrTy(fi->getContext()), 0), true);
                            Value *ptr_snk_cast = builder.CreateCast(castCode, ptr_snk, Type::getInt8PtrTy(fi->getContext()));
                            values.push_back(ptr_snk_cast);
                            // size of the move
                            auto size = call->getOperand(2);
                            castCode = CastInst::getCastOpcode(size, true, PointerType::get(Type::getInt8PtrTy(fi->getContext()), 0), true);
                            Value *size_cast = builder.CreateCast(castCode, size, Type::getInt64Ty(fi->getContext()));
                            values.push_back(size_cast);
                            // generate backend call
                            auto backendCall = builder.CreateCall(MemoryMov, values);
                            backendCall->moveBefore(call);
                            backendCall->setDebugLoc(NULL);
                        }
                        // libc memset
                        else if( call->getCalledFunction()->getName() == "memset" )
                        {
                            IRBuilder<> builder(call);
                            std::vector<Value *> values;
                            // src pointer
                            auto ptr_src = call->getOperand(0);
                            auto castCode = CastInst::getCastOpcode(ptr_src, true, PointerType::get(Type::getInt8PtrTy(fi->getContext()), 0), true);
                            Value *ptr_src_cast = builder.CreateCast(castCode, ptr_src, Type::getInt8PtrTy(fi->getContext()));
                            values.push_back(ptr_src_cast);
                            // size of the set
                            auto size = call->getOperand(2);
                            castCode = CastInst::getCastOpcode(size, true, PointerType::get(Type::getInt8PtrTy(fi->getContext()), 0), true);
                            Value *size_cast = builder.CreateCast(castCode, size, Type::getInt64Ty(fi->getContext()));
                            values.push_back(size_cast);
                            // generate backend call
                            auto backendCall = builder.CreateCall(MemorySet, values);
                            backendCall->moveBefore(call);
                            backendCall->setDebugLoc(NULL);
                        }
                        // libc malloc or stl new operator (two stl flavors)
                        else if( isAllocatingFunction(call) )
                        {
                            IRBuilder<> builder(call);
                            std::vector<Value *> values;
                            values.push_back(call);
                            // grab the size parameter so we can construct a memory tuple from this
                            auto size = call->getOperand(0);
                            auto castCode = CastInst::getCastOpcode(size, true, PointerType::get(Type::getInt64Ty(fi->getContext()), 0), true);
                            Value *size_cast = builder.CreateCast(castCode, size, Type::getInt64Ty(fi->getContext()));
                            values.push_back(size_cast);
                            auto backendCall = builder.CreateCall(MemoryMalloc, values);
                            // there are two injection cases
                            // 1. this is an invoke, then we just inject after
                            // 2. its just a regular function call, then we inject after the call
                            if( auto inv = llvm::dyn_cast<InvokeInst>(call) )
                            {
                                // only put the backend call at the regular landing site, if an exception occurred we want to ignore that pointer
                                auto regReturnSite = inv->getNormalDest()->getFirstNonPHIOrDbgOrLifetime();
                                backendCall->moveAfter(regReturnSite);
                            }
                            else
                            {
                                backendCall->moveAfter(call);
                            }
                            backendCall->setDebugLoc(NULL);
                        }
                        // libc free() or stl delete operator
                        else if( isFreeingFunction(call) )
                        {
                            IRBuilder<> builder(call);
                            std::vector<Value *> values;
                            auto addr = call->getOperand(0);
                            values.push_back(addr);
                            auto backendCall = builder.CreateCall(MemoryFree, values);
                            backendCall->moveBefore(call);
                            backendCall->setDebugLoc(NULL);
                        }
                    }
                }
            }
        }
        return true;
    }

    bool Memory::doInitialization(Module &M)
    {
        MemoryLoad      = cast<Function>(M.getOrInsertFunction("__TraceAtlas__Profile__Backend__MemoryLoad", Type::getVoidTy(M.getContext()), Type::getIntNPtrTy(M.getContext(), 8), Type::getInt64Ty(M.getContext()), Type::getInt32Ty(M.getContext()), Type::getInt64Ty(M.getContext())).getCallee());
        MemoryStore     = cast<Function>(M.getOrInsertFunction("__TraceAtlas__Profile__Backend__MemoryStore", Type::getVoidTy(M.getContext()), Type::getIntNPtrTy(M.getContext(), 8), Type::getInt64Ty(M.getContext()), Type::getInt32Ty(M.getContext()), Type::getInt64Ty(M.getContext())).getCallee());
        MemoryInit      = cast<Function>(M.getOrInsertFunction("__TraceAtlas__Profile__Backend__MemoryInit", Type::getVoidTy(M.getContext()), Type::getInt64Ty(M.getContext())).getCallee());
        MemoryDestroy   = cast<Function>(M.getOrInsertFunction("__TraceAtlas__Profile__Backend__MemoryDestroy", Type::getVoidTy(M.getContext())).getCallee());
        MemoryIncrement = cast<Function>(M.getOrInsertFunction("__TraceAtlas__Profile__Backend__MemoryIncrement", Type::getVoidTy(M.getContext()), Type::getInt64Ty(M.getContext())).getCallee());
        MemoryCpy       = cast<Function>(M.getOrInsertFunction("__TraceAtlas__Profile__Backend__MemoryCpy", Type::getVoidTy(M.getContext()), Type::getIntNPtrTy(M.getContext(), 8), Type::getIntNPtrTy(M.getContext(), 8), Type::getInt64Ty(M.getContext())).getCallee());
        MemoryMov       = cast<Function>(M.getOrInsertFunction("__TraceAtlas__Profile__Backend__MemoryMov", Type::getVoidTy(M.getContext()), Type::getIntNPtrTy(M.getContext(), 8), Type::getIntNPtrTy(M.getContext(), 8), Type::getInt64Ty(M.getContext())).getCallee());
        MemorySet       = cast<Function>(M.getOrInsertFunction("__TraceAtlas__Profile__Backend__MemorySet", Type::getVoidTy(M.getContext()), Type::getIntNPtrTy(M.getContext(), 8), Type::getInt64Ty(M.getContext())).getCallee());
        MemoryMalloc    = cast<Function>(M.getOrInsertFunction("__TraceAtlas__Profile__Backend__MemoryMalloc", Type::getVoidTy(M.getContext()), Type::getIntNPtrTy(M.getContext(), 8), Type::getInt64Ty(M.getContext())).getCallee());
        MemoryFree      = cast<Function>(M.getOrInsertFunction("__TraceAtlas__Profile__Backend__MemoryFree", Type::getVoidTy(M.getContext()), Type::getIntNPtrTy(M.getContext(), 8)).getCallee());
        return false;
    }

    void Memory::getAnalysisUsage(AnalysisUsage &AU) const
    {
        AU.addRequired<DashTracer::Passes::EncodedAnnotate>();
        AU.addRequired<DashTracer::Passes::MarkovIO>();
    }

    char Memory::ID = 0;
    static RegisterPass<Memory> Y("Memory", "Injects memory profiling to the binary", true, false);
} // namespace DashTracer::Passes