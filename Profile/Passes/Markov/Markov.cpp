// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
#include "Markov.h"
#include "Util/Annotate.h"
#include "Functions.h"
#include "LoopInfoDump.h"
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Type.h>
#include <llvm/Pass.h>
#include <llvm/Support/raw_ostream.h>
#include <set>
#include <string>
#include <vector>
#include <deque>

using namespace llvm;

std::set<BasicBlock *> exitCovered;

namespace DashTracer::Passes
{
    bool Markov::runOnFunction(Function &F)
    {
        for (auto fi = F.begin(); fi != F.end(); fi++)
        {
            auto *BB = cast<BasicBlock>(fi);
            int64_t id = GetBlockID(BB);
            auto firstInsertion = cast<Instruction>(BB->getFirstInsertionPt());
            IRBuilder<> firstBuilder(firstInsertion);

            // insert MarkovIncrement
            // skip this if we are in the first block of main
            if (!((F.getName() == "main") && (fi == F.begin())))
            {
                // if we are at the first block of a function, mark this as a function entrance increment
                std::vector<Value *> args;
                Value *idValue = ConstantInt::get(Type::getInt64Ty(BB->getContext()), (uint64_t)id);
                args.push_back(idValue);
                if( fi == F.begin() )
                {
                    Value *ent = ConstantInt::get(Type::getInt1Ty(BB->getContext()), true);
                    args.push_back(ent);
                }
                else
                {
                    Value *ent = ConstantInt::get(Type::getInt1Ty(BB->getContext()), false);
                    args.push_back(ent);
                }
                auto call = firstBuilder.CreateCall(MarkovIncrement, args);
                call->setDebugLoc(NULL);
            }

            // two things to check
            // first, see if this is the first block of main, and if it is, insert MarkovInit to the first instruction of the block
            // second, see if there is a return from main here, and if it is, insert MarkovDestroy and move it right before the ReturnInst
            if (F.getName() == "main")
            {
                // MarkovInit
                if (fi == F.begin())
                {
                    std::vector<Value *> args;
                    // get blockCount and make it a value in the LLVM Module
                    IRBuilder<> initBuilder(firstInsertion);
                    uint64_t blockCount = GetBlockCount(F.getParent());
                    Value *countValue = ConstantInt::get(Type::getInt64Ty(BB->getContext()), blockCount);
                    args.push_back(countValue);
                    // get the BBID and make it a value in the LLVM Module
                    Value *blockID = ConstantInt::get(Type::getInt64Ty(BB->getContext()), (uint64_t)id);
                    args.push_back(blockID);
                    auto call = initBuilder.CreateCall(MarkovInit, args);
                    call->setDebugLoc(NULL);
                }
                // MarkovDestroy
                // Place this before any return from main
                else if (auto retInst = dyn_cast<ReturnInst>(fi->getTerminator()))
                {
                    auto endInsertion = BB->getTerminator();
                    auto *lastInst = cast<Instruction>(endInsertion);
                    IRBuilder<> lastBuilder(lastInst);

                    auto call = lastBuilder.CreateCall(MarkovDestroy);
                    call->moveBefore(lastInst);
                    call->setDebugLoc(NULL);
                }
                else if (auto resumeInst = dyn_cast<ResumeInst>(fi->getTerminator()))
                {
                    auto endInsertion = BB->getTerminator();
                    auto *lastInst = cast<Instruction>(endInsertion);
                    IRBuilder<> lastBuilder(lastInst);

                    auto call = lastBuilder.CreateCall(MarkovDestroy);
                    call->moveBefore(lastInst);
                    call->setDebugLoc(NULL);
                }
                else if (auto unreachableInst = dyn_cast<UnreachableInst>(fi->getTerminator()))
                {
                    auto endInsertion = BB->getTerminator();
                    auto *lastInst = cast<Instruction>(endInsertion);
                    IRBuilder<> lastBuilder(lastInst);

                    auto call = lastBuilder.CreateCall(MarkovDestroy);
                    call->moveBefore(lastInst);
                    call->setDebugLoc(NULL);
                }
            }
            // inject MarkovDestroy calls anywhere we find a libc::exit() call
            for (auto bi = fi->begin(); bi != fi->end(); bi++)
            {
                if (auto call = dyn_cast<CallInst>(bi))
                {
                    if (call->getCalledFunction())
                    {
                        auto fname = call->getCalledFunction()->getName();
                        if (fname == "exit")
                        {
                            IRBuilder<> destroyInserter(call);
                            auto insert = destroyInserter.CreateCall(MarkovDestroy);
                            insert->moveBefore(call);
                            insert->setDebugLoc(NULL);
                        }
                    }
                }
            }
            // now we need to mark all the thread launch and join sites
            // we do this by looking for "pthread_create" and "pthread_join" or "pthread_exit"
            for( auto bi = fi->begin(); bi != fi->end(); bi++ )
            {
                enum InjectType {
                    None,
                    Launcher,
                    Joiner,
                    Exiter 
                };
                InjectType t = InjectType::None;
                // recursively investigate the call, looking for a pthread_create, pthread_join, or pthread_exit
                // if we hit a null function, we may have hit a blind spot, but there's nothing we can do about it here
                std::deque<llvm::User*> Q;
                std::set<llvm::User*> covered;
                if( auto call = llvm::dyn_cast<llvm::CallBase>(bi) )
                {
                    if( call->getCalledFunction() )
                    {
                        //if( !call->getCalledFunction()->empty() )
                        //{
                            Q.push_back(call);
                            covered.insert(call);
                        //}
                    }
                }
                // the while loop recursively implements the search for a pthread_ operation of interest (since they can be nested inside of args of function calls, like std::thread and omp do)
                while( !Q.empty() )
                {
                    if( auto call = llvm::dyn_cast<llvm::CallBase>(Q.front()) )
                    {
                        if( call->getCalledFunction() )
                        {
                            Q.push_back(call->getCalledFunction() );
                        }
                        // search through the args of the call instruction to see if we can go deeper into this call and find a thread launch/join/exit
                        for( unsigned i = 0; i < call->getNumOperands(); i++ )
                        {
                            auto arg = call->getOperand(i);
                            if( auto op = llvm::dyn_cast<llvm::BitCastOperator>(arg) )
                            {
                                Q.push_back(op);
                                covered.insert(op);
                            }
                        }
                    }
                    else if( auto op = llvm::dyn_cast<llvm::BitCastOperator>(Q.front()) )
                    {
                        for( unsigned i = 0; i < op->getNumOperands(); i++ )
                        {
                            auto arg = op->getOperand(i);
                            if( auto call = llvm::dyn_cast<llvm::Function>(arg) )
                            {
                                Q.push_back(call);
                                covered.insert(call);
                            }
                        }
                    }
                    else if( auto f = llvm::dyn_cast<llvm::Function>(Q.front()) )
                    {
                        // investigate the name, if it is a match we have found a place to inject
                        if( f->getName() == "pthread_create" )
                        {
                            t = InjectType::Launcher;
                        }
                        else if( f->getName() == "__kmpc_fork_call" )
                        {
                            t = InjectType::Launcher;
                        }
                        else if( f->getName() == "pthread_join" )
                        {
                            t = InjectType::Joiner;
                        }
                        else if( f->getName() == "pthread_exit" )
                        {
                            t = InjectType::Exiter;
                        }
                    }
                    if( t != InjectType::None )
                    {
                        // we are done searching, time to inject
                        break;
                    }
                    Q.pop_front();
                } // while (!Q.empty())
                if( t == InjectType::Launcher )
                {
                    // inject launcher function before the launch occurs
                    std::vector<Value *> args;
                    Value *blockID = ConstantInt::get(Type::getInt64Ty(BB->getContext()), (uint64_t)id);
                    args.push_back(blockID);
                    IRBuilder<> launchInserter(llvm::cast<Instruction>(bi));
                    auto insert = launchInserter.CreateCall(MarkovLaunch, args);
                    insert->moveBefore(llvm::cast<llvm::Instruction>(bi));
                    insert->setDebugLoc(NULL);
                }
                else if( t == InjectType::Joiner )
                {
                    // inject the joiner function before the join
                    // pthread_join has the pointer to the thread as its first argument, we need to intercept this argument in order to get the mapping 1:1
                    // right now we do anything for joins
                }
                else if( t == InjectType::Exiter )
                {
                    // inject the thread exit function before the exit
                    // pthread_exit() is called by the exiting thread, thus the "destination" of this call is open to interpretation (could be the thread launch site, could be where the execution of the spawning thread is at that point in time, could be nowhere)
                    // right now we don't do anything for thread exits
                }
                // else we are none, move on
            }
        } // for fi in F
        return true;
    }

    bool Markov::doInitialization(Module &M)
    {
        MarkovInit = cast<Function>(M.getOrInsertFunction("MarkovInit", Type::getVoidTy(M.getContext()), Type::getInt64Ty(M.getContext()), Type::getInt64Ty(M.getContext())).getCallee());
        MarkovDestroy = cast<Function>(M.getOrInsertFunction("MarkovDestroy", Type::getVoidTy(M.getContext())).getCallee());
        MarkovIncrement = cast<Function>(M.getOrInsertFunction("MarkovIncrement", Type::getVoidTy(M.getContext()), Type::getInt64Ty(M.getContext()), Type::getInt1Ty(M.getContext())).getCallee());
        MarkovLaunch = cast<Function>(M.getOrInsertFunction("MarkovLaunch", Type::getVoidTy(M.getContext()), Type::getInt64Ty(M.getContext())).getCallee());
        return false;
    }

    void Markov::getAnalysisUsage(AnalysisUsage &AU) const
    {
        AU.addRequired<DashTracer::Passes::LoopInfoDump>();
    }

    char Markov::ID = 0;
    static RegisterPass<Markov> Y("Markov", "Adds Markov Dumping to the binary", true, false);
} // namespace DashTracer::Passes