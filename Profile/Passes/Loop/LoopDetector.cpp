#include "AtlasUtil/Annotate.h"
#include "iostream"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/IR/Instructions.h>

using namespace llvm;
using namespace std;
namespace
{
    static int loopIDCounter = 0;
    struct CFG : public FunctionPass
    {
        static char ID; // Pass identification, replacement for typeid
        CFG() : FunctionPass(ID) {}

        void getAnalysisUsage(AnalysisUsage &AU) const override
        {
            AU.setPreservesCFG();
            AU.addRequired<LoopInfoWrapperPass>();
        }
        // mapping bb-id to line number set
        map<int, map<BasicBlock *, set<unsigned>>> LoopBBLineNumberMap;

        // mapping loop id to its type: 0-normal, 1-recursive in loop, 2-function pointer in loop
        // 3- loop in recursive, 4-loop in function pointer
        map<int, int> Looptype;

        vector<Function *> stackedCalledFunction;
        //return true if it should be filtered

        bool isFunctionPointerType(Type *type)
        {
            // Check the type here
            // if (PointerType *pointerType = dyn_cast<PointerType>(type))
            // {
            //     return isFunctionPointerType(pointerType->getElementType());
            // }
            // //Exit Condition
            // else if (type->isFunctionTy())
            // {
            //     return true;
            // }
            if (PointerType *pointerType = dyn_cast<PointerType>(type))
            {
                if (pointerType->getElementType()->isFunctionTy() == true)
                {
                    return true;
                }
            }
            return false;
        }

        bool stackCheck(vector<Function *> stackedCalledFunction, Function *F)
        {

            // if there is recursive function
            for (auto func : stackedCalledFunction)
            {
                if (func == F)
                {

                    return true;
                }
            }
            // if F is a function pointer
            // if(isFunctionPointerType(F->getType()))
            // {
            //     errs()<<"function pointer!"<<F->getName() <<"\n";
            //     return true;
            // }
            return false;
        }

        void functionCallInsideLoop(Function *F)
        {
            for (auto b = F->begin(); b != F->end(); b++)
            {
                auto *BB = cast<BasicBlock>(b);
                for (BasicBlock::iterator i = b->begin(), ie = b->end(); i != ie; ++i)
                {

                    Instruction *instruction = dyn_cast<Instruction>(i);
                    const llvm::DebugLoc &debugInfo = instruction->getDebugLoc();
                    if (debugInfo)
                    {
                        unsigned int line = debugInfo.getLine();
                        LoopBBLineNumberMap[loopIDCounter][BB].insert(line);
                    }
                    if (isa<CallInst>(&(*i)) || isa<InvokeInst>(&(*i)))
                    {
                        auto *calledFunction = cast<CallInst>(i)->getCalledFunction();

                        if (calledFunction == nullptr)
                        {
                            Looptype[loopIDCounter] = 2;
                            errs() << "\n\n\n function pointer in loop " << loopIDCounter << "!!!!!\n\n\n";
                            return;
                        }

                        if (!stackCheck(stackedCalledFunction, calledFunction))
                        {
                            stackedCalledFunction.push_back(calledFunction);
                            functionCallInsideLoop(calledFunction);
                        }
                        else
                        {
                            errs() << "\n\n\n recursive in loop " << loopIDCounter << "!!!!!\n\n\n";
                            Looptype[loopIDCounter] = 1;
                            return;
                        }
                    }
                }
            }
            stackedCalledFunction.pop_back();
        }

        int CheckLoopID(BasicBlock *b)
        {

            for (auto loop : LoopBBLineNumberMap)
            {
                // errs() << "loop:" << loop.first << "\n";
                // errs() << "type:" << Looptype[loop.first] << "\n";
                for (auto lp : loop.second)
                {
                    // errs() << "bb:";
                    // lp.first->printAsOperand(errs(), false);
                    // errs() << "\n";
                    // errs() << "line:";
                    if (lp.first == b)
                    {
                        return loop.first;
                    }
                }
            }
            return -1;
        }

        void functionCallCheck(Function *F, LoopInfo &LI)
        {
            bool loopInside = false;
            BasicBlock *loopBB;
            for (auto b = F->begin(); b != F->end(); b++)
            {
                auto *BB = cast<BasicBlock>(b);
                if (LI.getLoopFor(BB))
                {
                    loopInside = true;
                    loopBB = BB;
                }

                for (BasicBlock::iterator i = b->begin(), ie = b->end(); i != ie; ++i)
                {

                    if (isa<CallInst>(&(*i)) || isa<InvokeInst>(&(*i)))
                    {
                        auto *calledFunction = cast<CallInst>(i)->getCalledFunction();

                        if (calledFunction == nullptr)
                        {

                            if (LI.getLoopFor(BB))
                            {
                                int loopid = CheckLoopID(BB);
                                Looptype[loopid] = 4;
                                errs() << "\n\n\n loop in function pointer  " << loopid << "!!!!!\n\n\n";
                                return;
                            }
                        }

                        if (!stackCheck(stackedCalledFunction, calledFunction))
                        {
                            stackedCalledFunction.push_back(calledFunction);
                            functionCallCheck(calledFunction, LI);
                        }
                        else
                        {
                            if (loopInside)
                            {
                                int loopid = CheckLoopID(loopBB);
                                Looptype[loopid] = 3;
                                errs() << "\n\n\n loop " << loopid << " is in recursive!!!!!\n\n\n";
                                return;
                            }
                        }
                    }
                }
            }
            stackedCalledFunction.pop_back();
        }

        void BlocksInLoop(Loop *L, unsigned nlvl)
        {
            loopIDCounter++;

            // initial loop type is normal, if it is not then will be set to other values later
            Looptype[loopIDCounter] = 0;

            // errs() << "Loop level" << nlvl << " {\n";
            // BasicBlock *h = L->getHeader();
            // ScalarEvolution *SE = &getAnalysis <
            // ScalarEvolutionWrapperPass().getSE(); errs() << "Loop trip count :" <<
            // SE->getSmallConstantTripCount(L) << "\n";
            std::vector<Loop *> subLoops = L->getSubLoops();
            Loop::iterator j, f;
            for (j = subLoops.begin(), f = subLoops.end(); j != f; ++j)
            {
                BlocksInLoop(*j, nlvl + 1);
            }
            // unsigned numBlocks = 0;
            Loop::block_iterator bb;
            for (bb = L->block_begin(); bb != L->block_end(); ++bb)
            {
                BasicBlock *BB = *bb;
                for (BasicBlock::iterator i = BB->begin(), ie = BB->end(); i != ie; ++i)
                {
                    Instruction *instruction = dyn_cast<Instruction>(i);
                    const llvm::DebugLoc &debugInfo = instruction->getDebugLoc();
                    if (debugInfo)
                    {
                        unsigned int line = debugInfo.getLine();
                        LoopBBLineNumberMap[loopIDCounter][BB].insert(line);
                    }
                    if (isa<CallInst>(&(*i)) || isa<InvokeInst>(&(*i)))
                    {
                        // auto *call = cast<CallInst>(i);
                        auto *calledFunction = cast<CallInst>(i)->getCalledFunction();
                        // errs()<<"Call "<< call->getCalledFunction()->getName() << "\n";
                        if (calledFunction != nullptr)
                        {
                            stackedCalledFunction.push_back(calledFunction);
                            functionCallInsideLoop(calledFunction);
                        }
                        else
                        {
                            Looptype[loopIDCounter] = 2;
                            errs() << "\n\n\n function pointer in loop " << loopIDCounter << "!!!!!\n\n\n";
                            return;
                        }
                    }
                }
            }
        }

        bool runOnFunction(Function &F) override
        {

            LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
            LI.print(errs());

            for (LoopInfo::iterator i = LI.begin(), e = LI.end(); i != e; ++i)
            {
                BlocksInLoop(*i, 0);
            }
            for (auto loop : LoopBBLineNumberMap)
            {
                errs() << "loop:" << loop.first << "\n";
                errs() << "type:" << Looptype[loop.first] << "\n";
                for (auto lp : loop.second)
                {
                    errs() << "bb:";
                    lp.first->printAsOperand(errs(), false);
                    errs() << "\n";
                    errs() << "line:";
                    for (auto line : lp.second)
                    {
                        errs() << line << "\t";
                    }
                    errs() << "\n";
                }
                errs() << "\n";
            }
            errs() << "\n";

            // errs() << "loopBegin{ \n";
            for (auto b = F.begin(); b != F.end(); b++)
            {
                //auto *BB = cast<BasicBlock>(b);
                // bool isLoop = LI.getLoopFor(BB);

                for (BasicBlock::iterator i = b->begin(), ie = b->end(); i != ie; ++i)
                {
                    if (isa<CallInst>(&(*i)) || isa<InvokeInst>(&(*i)))
                    {
                        // auto *call = cast<CallInst>(i);
                        auto *calledFunction = cast<CallInst>(i)->getCalledFunction();
                        // errs()<<"Call "<< call->getCalledFunction()->getName() << "\n";
                        if (calledFunction != nullptr)
                        {
                            stackedCalledFunction.push_back(calledFunction);
                            functionCallCheck(calledFunction, LI);
                        }
                        else
                        {
                            StringRef fname = i->getParent()->getName();
                            errs() << "\n\n\n loop" << fname << "is in function pointer!!!!!\n\n\n";
                            // if (LI.getLoopFor(BB))
                            // {
                            //     int loopid = CheckLoopID(BB);
                            //     Looptype[loopid] = 4;
                            //     errs() << "\n\n\n loop " << loopid << " is in function pointer!!!!!\n\n\n";
                            // }
                        }
                    }
                }

                // if (isLoop)
                // {
                // int64_t blockId = GetBlockID(BB);
                // errs() << "bb:" << blockId<<"\n";
                // errs() << "bb:";
                // BB->printAsOperand(errs(), false);
                // errs() << "\n";
                // errs() << "line number :";
                //     for (BasicBlock::iterator i = b->begin(), ie = b->end(); i != ie; ++i)
                //     {

                //         Instruction *instruction = dyn_cast<Instruction>(i);
                //         const llvm::DebugLoc &debugInfo = instruction->getDebugLoc();
                //         if (debugInfo)
                //         {
                //             unsigned int line = debugInfo.getLine();
                //             errs() << line << ",";
                //         }
                //     }
                //     errs() << "\n";
                // }
            }
            // errs() << "}loopEnd\n";
            return false;
        }
    };
} // namespace

char CFG::ID = 0;
static RegisterPass<CFG> X("CFG", "Gen CFG", true, true);