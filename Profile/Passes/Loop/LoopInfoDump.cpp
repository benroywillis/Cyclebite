//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "LoopInfoDump.h"
#include "LoopInfoDump.h"
#include "Util/Annotate.h"
#include "Util/Print.h"
#include <fstream>
#include <iomanip>
#include <llvm/Analysis/AssumptionCache.h>
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/Analysis/ScalarEvolution.h>
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/IR/Dominators.h>
#include <nlohmann/json.hpp>

using namespace llvm;
using namespace std;
/*
namespace Cyclebite::Profile::Passes
{
    enum class LoopType
    {
        // Normal static analysis tools will be able to detect this loop
        Normal,
        // loop contains a recursive function call
        RecursiveLoop,
        // recursive function call contains loop
        LoopRecursive,
        // Loop contains a function pointer
        FpInLoop,
        // Parent function of loop is called through an indirect call
        LoopInFp
    };
    map<Loop *, LoopType> Looptype;
    // stack emulator to find recursive and indirect-recursive functions
    vector<Function *> stackedCalledFunction;
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
        return false;
    }

    void functionCallInsideLoop(Loop *L, Function *F)
    {
        for (auto b = F->begin(); b != F->end(); b++)
        {
            for (BasicBlock::iterator i = b->begin(), ie = b->end(); i != ie; ++i)
            {
                if (auto call = dyn_cast<CallBase>(&(*i)))
                {
                    auto *calledFunction = call->getCalledFunction();
                    if (calledFunction == nullptr)
                    {
                        // loop contains a function pointer
                        Looptype[L] = LoopType::FpInLoop;
                        return;
                    }
                    if (!stackCheck(stackedCalledFunction, calledFunction))
                    {
                        stackedCalledFunction.push_back(calledFunction);
                        functionCallInsideLoop(L, calledFunction);
                    }
                    else
                    {
                        // loop contains a call to a recursive function
                        Looptype[L] = LoopType::RecursiveLoop;
                        return;
                    }
                }
            }
        }
        stackedCalledFunction.pop_back();
    }

    void functionCallCheck(Function *F, LoopInfo &LI)
    {
        bool loopInside = false;
        for (auto b = F->begin(); b != F->end(); b++)
        {
            auto *BB = cast<BasicBlock>(b);
            auto BBloop = LI.getLoopFor(BB);
            if (LI.getLoopFor(BB))
            {
                loopInside = true;
            }
            for (BasicBlock::iterator i = b->begin(), ie = b->end(); i != ie; ++i)
            {
                if (auto call = dyn_cast<CallBase>(&(*i)))
                {
                    auto *calledFunction = call->getCalledFunction();
                    if (calledFunction == nullptr)
                    {
                        if (LI.getLoopFor(BB))
                        {
                            // parent function of loop is called indirectly
                            Looptype[BBloop] = LoopType::LoopInFp;
                        }
                        return;
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
                            // parent function of loop is a recursive function
                            Looptype[BBloop] = LoopType::LoopRecursive;
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
        // initial loop type is normal, if it is not then will be set to other values later
        Looptype[L] = LoopType::Normal;
        std::vector<Loop *> subLoops = L->getSubLoops();
        Loop::iterator j, f;
        for (j = subLoops.begin(), f = subLoops.end(); j != f; ++j)
        {
            BlocksInLoop(*j, nlvl + 1);
        }
        for (auto bb = L->block_begin(); bb != L->block_end(); ++bb)
        {
            BasicBlock *BB = *bb;
            for (BasicBlock::iterator i = BB->begin(), ie = BB->end(); i != ie; ++i)
            {
                if (auto call = dyn_cast<CallBase>(&(*i)))
                {
                    auto *calledFunction = call->getCalledFunction();
                    if (calledFunction != nullptr)
                    {
                        stackedCalledFunction.push_back(calledFunction);
                        functionCallInsideLoop(L, calledFunction);
                    }
                    else
                    {
                        // loop contains a function pointer
                        Looptype[L] = LoopType::FpInLoop;
                        return;
                    }
                }
            }
        }
    }

    bool LoopInfoDump::runOnModule(Module &M)
    {
        // good reference for static IV analysis at https://github.com/hecmay/llvm-pass-skeleton/blob/master/skeleton/Skeleton.cpp
        // comes from this project https://www.cs.cornell.edu/courses/cs6120/2019fa/blog/strength-reduction-pass-in-llvm/
        map<uint64_t, map<string, vector<int64_t>>> loops;
        set<int64_t> staticBlocks;
        uint64_t loopID = 0;
        for (auto &f : M)
        {
            if (f.empty())
            {
                continue;
            }
            LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>(f).getLoopInfo();

            // LL - loop type discovery
            for (LoopInfo::iterator i = LI.begin(), e = LI.end(); i != e; ++i)
            {
                BlocksInLoop(*i, 0);
            }
            for (auto b = f.begin(); b != f.end(); b++)
            {
                for (BasicBlock::iterator i = b->begin(), ie = b->end(); i != ie; ++i)
                {
                    if (auto call = dyn_cast<CallBase>(&(*i)))
                    {
                        auto *calledFunction = call->getCalledFunction();
                        if (calledFunction != nullptr)
                        {
                            stackedCalledFunction.push_back(calledFunction);
                            functionCallCheck(calledFunction, LI);
                        }
                    }
                }
            }
            // John: on where we should be taking this induction variable stuff
            // would it actually be helpful to know the induction variable?
            // - would it be present in a reasonable number of cases?
            //   -> probably not.. in the places where we would have a contribution, it likely won't be affine
            // - would it contain information we need?
            //   -> for affine loops, the induction variable is used to offset a base pointer... thus if I find the induction variable I find the base pointer
            //   -> vectorization opportunities
            // - can I just go to the base pointer?
            // example: I want to infer vector add, but I have linked-list add
            // - I actually don't care.. what is the essence of the underlying data structure, and what is the ordering of the algorithm?
            // So then what gives me the most information?
            // - base pointers and their modifiers (sequence)
            // - extracting the function
            // What happens with GEMM or image convolution
            // - k is a fake one
            //   -> GEMM is actually arbitrary element-wise muls
            //   -> k is a reduction along the muls
            // - k and l are fake ones too
            // MVP goal: I found i and j in GEMM, i, j, ii, jj in image conv.. thus I know who is offset by them and therefore what the base pointers are
            // secong MVP goal: what does the iterator function look like.. ie this is the ordering that must be followed for the algorithm
            // Ben - loop induction variable
            // we use the induction variable to find the base pointers

            // John 2/22/2022
            // How will we validate our IV/BP finding?
            //  - polyhedral model
            //    -> if I can find boundaries for this variable, this should be an induction variable
            //  - generic answers that are still checkable
            //    -> map induction variable back to source code line
            set<Instruction *> basePointers;
            auto &TLI = getAnalysis<TargetLibraryInfoWrapperPass>();
            auto AC = AssumptionCache(f);
            auto DT = DominatorTree(f);
            auto tli = TLI.getTLI(f);
            auto SE = ScalarEvolution(f, tli, AC, DT, LI);
            for (auto loop : LI)
            {
                //for( auto b : loop->blocks() )
                //{
                //    PrintVal(cast<BasicBlock>(b));
                //}
                auto IV = loop->getInductionVariable(SE);
                // common checks
                if (loop->isLoopSimplifyForm())
                {
                    //cout << "Loop is simple!" << endl;
                }
                else
                {
                    if (!loop->getLoopPreheader())
                    {
                        cout << "Loop does not have a pre-header" << endl;
                    }
                    //else if (BasicBlock *Latch = loop->getLoopLatch())
                    //{
                    //    PrintVal(Latch);
                    //    if (BranchInst *BI = dyn_cast_or_null<BranchInst>(Latch->getTerminator()))
                    //        if (BI->isConditional())
                    //            cout << "Latch is correct!" << endl;
                    //        else
                    //            cout << "Latch is not correct:(" << endl;
                    //    else
                    //        cout << "Latch is not terminated with a branch:(" << endl;
                    //}
                    else if (!loop->getLoopLatch())
                    {
                        cout << "Loop latch could not be found" << endl;
                    }
                    else if (!loop->hasDedicatedExits())
                    {
                        cout << "Loop has exits whose predecessors do not exist within the loop" << endl;
                    }
                    else
                    {
                        cout << "Could not figure out why loop is not in simplify form" << endl;
                    }
                    for (auto b : loop->getBlocks())
                    {
                        PrintVal(b);
                    }
                }
                //
                if (IV)
                {
                    loops[loopID]["IV"].push_back(Cyclebite::Util::GetValueID(IV));
                    // the phi node inst gives the value that represents the induction variable
                    // this value should be used to offset the base pointer with GEPOperators

                    // it is possible for induction variables to be mangled by multiplications or extra additions after they are phi'd
                    // therefore we need to be recursive in this method
                    // the recursion terminates when we either find a gep or run out of users (in the relative context)
                    deque<Instruction *> Q;
                    set<Instruction *> covered;
                    covered.insert(IV);
                    Q.push_front(IV);
                    while (!Q.empty())
                    {
                        for (auto u : Q.front()->users())
                        {
                            if (auto gep = dyn_cast<GEPOperator>(u))
                            {
                                // these geps are likely offsetting a base pointer
                                // to find the base pointer, we have to trace the operand
                                auto ptr = gep->getPointerOperand();
                                if (auto base = dyn_cast<Instruction>(ptr))
                                {
                                    // we are looking for the root value of the data flow graph ie the node that has no predecessors
                                    //PrintVal(base);
                                    basePointers.insert(base);
                                }
                            }
                            else if (auto inst = dyn_cast<Instruction>(u))
                            {
                                // we recurse into the users of the current user and add them to the Q
                                for (auto user : inst->users())
                                {
                                    if (auto userinst = dyn_cast<Instruction>(user))
                                    {
                                        if (covered.find(userinst) == covered.end())
                                        {
                                            Q.push_back(userinst);
                                            covered.insert(userinst);
                                        }
                                    }
                                }
                            }
                        }
                        Q.pop_front();
                    }
                }
            }
            // Ben - function finding
            // John
            // What problem is this solving? (Can we describe the components? Can we come up with a mathematical expression?) (Karp's problems... can I mathematically describe the problem? What is the solution string?)
            // Problem: interpret the loop operation into a mathematical expression
            // Solution: I want to identify the operation(s) in the DFG that represents the function
            //  - John: it sounds like you are looking for equivalence in function...
            //          -> If I am looking for FFT, I'm looking for a butterfly
            //          -> If I am looking for GEMM I am looking for MAC across 3 dimensions
            //          -> If I am looking for conv I am looking for MAC across 4 dimensions
            // - Constraints
            //   -> we are not allowed to reach outside the loop
            //   -> operations need to be done on the base pointer
            //   -> inputs need to come from the base pointer and results need to be recorded in the base pointer
            //   -> the induction variable must offset the base pointer
            //   -> no cuts in the dataflow graph (the store is reachable from the load)
            // Dangles: paths that may not go from load to store, and paths that lead to the store that did not include the load
            //  - for now just include them (for example calculating twiddle factors)
            // John: this might look like the spanning graph problem
            set<Instruction *> operations;
            for (auto base : basePointers)
            {
                // we look through all values in the DFG starting with the base pointers and ending when we run out of users of those base pointers
                // users of those base pointers include the load instructions that get the underlying values
                // the instructions that mangle these values are the "functions" of the loop
                // we will likely end up with many of these functions (eg gep -> load -> gep -> load -> cast -> mul -> add -> store)
                deque<Instruction *> Q;
                set<User *> covered;
                Q.push_front(base);
                covered.insert(base);
                while (!Q.empty())
                {
                    for (auto u : Q.front()->users())
                    {
                        // a user is considered a "function" of the loop if it does a binary, or comparing op on the base pointer
                        if (covered.find(u) != covered.end())
                        {
                            continue;
                        }
                        else if (auto un = dyn_cast<UnaryInstruction>(u))
                        {
                            // this can be an alloca, cast, ExtractValue, freeze, load, unary or VAArg inst
                            Q.push_back(un);
                            //operations.insert(un);
                        }
                        else if (auto bin = dyn_cast<BinaryOperator>(u))
                        {
                            // this can be arithmetic [add, fadd, sub, fsub, mul, fmul, udiv, sdiv, fdiv, urem, srem, frem] or bitwise binary [shl, lshr, ashr, and, or, xor] operation
                            //PrintVal(u);
                            Q.push_back(bin);
                            operations.insert(bin);
                        }
                        else if (auto cmp = dyn_cast<CmpInst>(u))
                        {
                            //PrintVal(u);
                            Q.push_back(cmp);
                            operations.insert(cmp);
                        }
                        else if (auto gep = dyn_cast<GetElementPtrInst>(u))
                        {
                            Q.push_back(gep);
                            //operations.insert(gep);
                        }
                        else if (auto st = dyn_cast<StoreInst>(u))
                        {
                            Q.push_back(st);
                            covered.insert(st);
                            //operations.insert(st);
                        }
                        covered.insert(u);
                    }
                    Q.pop_front();
                }
            }

            // Ben - loop info dump
            for (auto base : basePointers)
            {
                loops[loopID]["BasePointers"].push_back(Cyclebite::Util::GetValueID(base));
            }
            for (auto op : operations)
            {
                loops[loopID]["Functions"].push_back(Cyclebite::Util::GetValueID(op));
            }
            for (auto loop : LI)
            {
                set<llvm::Function *> covered;
                for (auto block : loop->blocks())
                {
                    // John: we cannot trivially say that the parent loop is hot if the child loop is hot (exception: loop body of parent loop is entirely the child loop, like GEMM, but in GEMM specifically, the middle loop should be hot)
                    // any loop that contained hot code in its call graph but did not rely on function indirect or recursion was a hot loop
                    // if a loop is contained within a recursion, it doesn't count, because I can't tell if it was hot because of the loop or because of the recursion
                    loops[loopID]["Blocks"].push_back(Cyclebite::Util::GetBlockID(block));
                    // add any functions present in the loop that does not contain another loop
                    deque<llvm::Function *> Q;
                    for (auto ii = block->begin(); ii != block->end(); ii++)
                    {
                        if (auto call = llvm::dyn_cast<CallBase>(ii))
                        {
                            if (call->getCalledFunction())
                            {
                                if (!call->getCalledFunction()->empty())
                                {
                                    if (covered.find(call->getCalledFunction()) == covered.end())
                                    {
                                        Q.push_back(call->getCalledFunction());
                                        covered.insert(call->getCalledFunction());
                                    }
                                }
                            }
                        }
                    }
                    while (!Q.empty())
                    {
                        for (auto b = Q.front()->begin(); b != Q.front()->end(); b++)
                        {
                            loops[loopID]["Blocks"].push_back(Cyclebite::Util::GetBlockID(llvm::cast<llvm::BasicBlock>(b)));
                            for (auto ii = b->begin(); ii != b->end(); ii++)
                            {
                                if (auto call = llvm::dyn_cast<CallBase>(ii))
                                {
                                    if (call->getCalledFunction())
                                    {
                                        if (!call->getCalledFunction()->empty())
                                        {
                                            if (covered.find(call->getCalledFunction()) == covered.end())
                                            {
                                                Q.push_back(call->getCalledFunction());
                                                covered.insert(call->getCalledFunction());
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        Q.pop_front();
                    }
                }
                // now take out all blocks from other loops
                loops[loopID]["Type"].push_back(static_cast<int>(Looptype[loop]));
                loopID++;
            }
            for (auto b = f.begin(); b != f.end(); b++)
            {
                staticBlocks.insert(Cyclebite::Util::GetBlockID(cast<BasicBlock>(b)));
            }
        }

        nlohmann::json loopfile;
        for (auto loop : loops)
        {
            loopfile["Loops"].push_back(loop.second);
        }
        loopfile["Static Blocks"] = staticBlocks;
        char *loopFileName = getenv("LOOP_FILE");
        if (!loopFileName)
        {
            loopFileName = string("Loopfile.json").data();
        }
        ofstream file;
        file.open(loopFileName);
        file << setw(4) << loopfile;
        return true;
    }

    void LoopInfoDump::getAnalysisUsage(AnalysisUsage &AU) const
    {
        AU.setPreservesCFG();
        AU.addRequired<Cyclebite::Profile::Passes::LoopInfoDump>();
        AU.addRequired<LoopInfoWrapperPass>();
        AU.addRequired<TargetLibraryInfoWrapperPass>();
    }
    char LoopInfoDump::LoopInfoDump::ID = 0;
    static RegisterPass<LoopInfoDump> X("LoopInfoDump", "Dumps information about loop info", true, true);
} // namespace Cyclebite::Profile::Passes
*/

// new pass manager registration
llvm::PassPluginLibraryInfo getLoopInfoDumpPluginInfo() 
{
    return {LLVM_PLUGIN_API_VERSION, "LoopInfoDump", LLVM_VERSION_STRING, 
        [](PassBuilder &PB) 
        {
            PB.registerPipelineParsingCallback( 
                [](StringRef Name, ModulePassManager& MPM, ArrayRef<PassBuilder::PipelineElement>) 
                {
                    if (Name == "LoopInfoDump") {
                        MPM.addPass(Cyclebite::Profile::Passes::LoopInfoDump());
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
    return getLoopInfoDumpPluginInfo();
}