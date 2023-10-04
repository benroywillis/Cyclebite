// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
#include "Precision.h"
#include "Backend/Precision/inc/Precision.h"
#include "Annotate.h"
#include "Util/Annotate.h"
#include "Util/Print.h"
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
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <deque>

using namespace llvm;
using namespace std;

namespace DashTracer::Passes
{
    void PassToBackend(llvm::IRBuilder<>& builder, llvm::Value* val, llvm::Function* fi, uint64_t blockId, uint32_t idx)
    {
        vector<llvm::Value*> values;
        // value
        if( val->getType()->isIntegerTy() )
        {
            // for ints
            if( val->getType()->getIntegerBitWidth() < 64 )
            {
                auto ext = builder.CreateZExtOrBitCast(val, Type::getInt64Ty(fi->getContext()));
                values.push_back(ext);
            } 
            // for longs
            else
            {
                values.push_back(val);
            }
        }
        else if( val->getType()->isFloatTy() )
        {
            auto code  = CastInst::getCastOpcode(val, true, Type::getDoubleTy(fi->getContext()), true);
            auto ext   = builder.CreateCast( code, val, Type::getDoubleTy(fi->getContext()) );
            auto toInt = builder.CreateBitOrPointerCast(ext, Type::getInt64Ty(fi->getContext()));
            values.push_back(toInt);
        }
        else if( val->getType()->isDoubleTy() )
        {
            auto valueCast  = builder.CreateBitOrPointerCast(val, Type::getInt64Ty(fi->getContext()));
            values.push_back(valueCast);
        }
        else if( val->getType()->isArrayTy() )
        {
            // skip
            return;
        }
        else if( val->getType()->isPointerTy() )
        {
            // skip
            return;
        }
        else
        {
            PrintVal(val);
            spdlog::critical("Cannot handle this type for dynamic range analysis!");
            throw AtlasException("Cannot handle this type for dynamic range analysis!");
        }
        //bb id
        Value *blockID = ConstantInt::get(Type::getInt64Ty(fi->getContext()), (uint64_t)blockId);
        values.push_back(blockID);
        // instruction index
        Value *instructionID = ConstantInt::get(Type::getInt32Ty(fi->getContext()), idx);
        values.push_back(instructionID);
        // data type
        Value *dataType = ConstantInt::get( Type::getInt8Ty(fi->getContext()), static_cast<uint8_t>( Cyclebite::Profile::Backend::Precision::LLVMTy2PrecisionTy(val->getType()) ));
        values.push_back(dataType);
        auto call = builder.CreateCall(PrecisionLoad, values);
        call->setDebugLoc(NULL);
    }
    bool Precision::runOnFunction(Function &F)
    {
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
                auto call = firstBuilder.CreateCall(PrecisionIncrement, args);
                call->setDebugLoc(NULL);
            }

            // two things to check
            // first, see if this is the first block of main, and if it is, insert PrecisionInit to the first instruction of the block
            // second, see if there is a return from main here, and if it is, insert PrecisionDestroy and move it right before the ReturnInst
            if (F.getName() == "main")
            {
                // PrecisionInit
                if (fi == F.begin())
                {
                    std::vector<Value *> args;
                    // get blockCount and make it a value in the LLVM Module
                    firstInsertion = BB->getFirstInsertionPt();
                    firstInst = cast<Instruction>(firstInsertion);
                    IRBuilder<> initBuilder(firstInst);
                    // get the BBID and make it a value in the LLVM Module
                    //Value *blockID = ConstantInt::get(Type::getInt64Ty(BB->getContext()), (uint64_t)blockId);
                    //args.push_back(blockID);
                    auto call = initBuilder.CreateCall(PrecisionInit, args);
                    call->setDebugLoc(NULL);
                }
                // PrecisionDestroy
                // Place this before any return from main
                else if (auto retInst = dyn_cast<ReturnInst>(fi->getTerminator()))
                {
                    auto endInsertion = BB->getTerminator();
                    auto *lastInst = cast<Instruction>(endInsertion);
                    IRBuilder<> lastBuilder(lastInst);

                    auto call = lastBuilder.CreateCall(PrecisionDestroy);
                    call->moveBefore(lastInst);
                    call->setDebugLoc(NULL);
                }
                else if (auto resumeInst = dyn_cast<ResumeInst>(fi->getTerminator()))
                {
                    auto endInsertion = BB->getTerminator();
                    auto *lastInst = cast<Instruction>(endInsertion);
                    IRBuilder<> lastBuilder(lastInst);

                    auto call = lastBuilder.CreateCall(PrecisionDestroy);
                    call->moveBefore(lastInst);
                    call->setDebugLoc(NULL);
                }
                else if (auto unreachableInst = dyn_cast<UnreachableInst>(fi->getTerminator()))
                {
                    auto endInsertion = BB->getTerminator();
                    auto *lastInst = cast<Instruction>(endInsertion);
                    IRBuilder<> lastBuilder(lastInst);

                    auto call = lastBuilder.CreateCall(PrecisionDestroy);
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
                            auto insert = destroyInserter.CreateCall(PrecisionDestroy);
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
                if (auto *load = dyn_cast<LoadInst>(CI))
                {
                    IRBuilder<> builder(load->getNextNode());
                    auto intercept = load;
                    if( intercept->getType()->isVectorTy() )
                    {
                        for( unsigned i = 0; i < intercept->getType()->getVectorNumElements(); i++ )
                        {
                            auto extracted = builder.CreateExtractElement(intercept, i);
                            PassToBackend(builder, extracted, llvm::cast<llvm::Function>(fi), (uint64_t)blockId, ldInstructionIndex);
                        }
                    }
                    else
                    {
                        PassToBackend(builder, intercept, llvm::cast<llvm::Function>(fi), (uint64_t)blockId, ldInstructionIndex);
                    }
                    ldInstructionIndex++;
                }
                else if (auto *store = dyn_cast<StoreInst>(CI))
                {
                    IRBuilder<> builder(store);
                    Value *stVal = store->getValueOperand();
                    if( stVal->getType()->isVectorTy() )
                    {
                        for( unsigned i = 0; i < stVal->getType()->getVectorNumElements(); i++ )
                        {
                            auto extracted = builder.CreateExtractElement(stVal, i);
                            PassToBackend(builder, extracted, llvm::cast<llvm::Function>(fi), (uint64_t)blockId, stInstructionIndex);
                        }
                    }
                    else
                    {
                        PassToBackend(builder, stVal, llvm::cast<llvm::Function>(fi), (uint64_t)blockId, stInstructionIndex);
                    }
                    stInstructionIndex++;
                }
            }
        }
        return true;
    }

    bool Precision::doInitialization(Module &M)
    {
        PrecisionIncrement = cast<Function>(M.getOrInsertFunction("__Cyclebite__Profile__Backend__PrecisionIncrement", Type::getVoidTy(M.getContext()), Type::getInt64Ty(M.getContext())).getCallee());
        //PrecisionLoad      = cast<Function>(M.getOrInsertFunction("__Cyclebite__Profile__Backend__PrecisionLoad", Type::getVoidTy(M.getContext()), Type::getIntNPtrTy(M.getContext(), 8), Type::getInt64Ty(M.getContext()), Type::getInt32Ty(M.getContext()), Type::getInt8Ty(M.getContext())).getCallee());
        //PrecisionStore     = cast<Function>(M.getOrInsertFunction("__Cyclebite__Profile__Backend__PrecisionStore", Type::getVoidTy(M.getContext()), Type::getIntNPtrTy(M.getContext(), 8), Type::getInt64Ty(M.getContext()), Type::getInt32Ty(M.getContext()), Type::getInt8Ty(M.getContext())).getCallee());
        PrecisionLoad      = cast<Function>(M.getOrInsertFunction("__Cyclebite__Profile__Backend__PrecisionLoad", Type::getVoidTy(M.getContext()), Type::getInt64Ty(M.getContext()), Type::getInt64Ty(M.getContext()), Type::getInt32Ty(M.getContext()), Type::getInt8Ty(M.getContext())).getCallee());
        PrecisionStore     = cast<Function>(M.getOrInsertFunction("__Cyclebite__Profile__Backend__PrecisionStore", Type::getVoidTy(M.getContext()), Type::getInt64Ty(M.getContext()), Type::getInt64Ty(M.getContext()), Type::getInt32Ty(M.getContext()), Type::getInt8Ty(M.getContext())).getCallee());
        PrecisionInit      = cast<Function>(M.getOrInsertFunction("__Cyclebite__Profile__Backend__PrecisionInit", Type::getVoidTy(M.getContext())).getCallee());
        PrecisionDestroy   = cast<Function>(M.getOrInsertFunction("__Cyclebite__Profile__Backend__PrecisionDestroy", Type::getVoidTy(M.getContext())).getCallee());
        return false;
    }

    void Precision::getAnalysisUsage(AnalysisUsage &AU) const
    {
        AU.addRequired<DashTracer::Passes::EncodedAnnotate>();
        AU.addRequired<DashTracer::Passes::MarkovIO>();
    }

    char Precision::ID = 0;
    static RegisterPass<Precision> Y("Precision", "Injects Precision profiling to the binary", true, false);
} // namespace DashTracer::Passes