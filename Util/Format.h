#pragma once
#include "Util/Annotate.h"
#include "Util/Split.h"
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/Utils.h>
#include <llvm/Transforms/Utils/LoopSimplify.h>
#include <llvm/Transforms/Scalar/LoopPassManager.h>
#include <llvm/Transforms/Scalar/DCE.h>
#include <llvm/Transforms/Scalar/SCCP.h>
#include <llvm/Transforms/Scalar/IndVarSimplify.h>
#include <llvm/Passes/PassBuilder.h>
#include <iostream>

inline void Format(llvm::Module *M, bool clean = true)
{
    // Ben [4/13/22]
    // useful transforms for bitcode simplification
    // all transforms need to be done to the bitcode before the Annotate pass is run
    std::cout << "Made it to the format function" << std::endl;
    llvm::FunctionPassManager FPM;
    // induction variable simplification
    /*FPM.addPass(llvm::createFunctionToLoopPassAdaptor(llvm::IndVarSimplifyPass()));
    // transforms loops to simplest form llvm knows of, defined at https://llvm.org/docs/LoopTerminology.html
    FPM.addPass(llvm::LoopSimplifyPass());
    // constant propogation/dead code elimination pass
    FPM.addPass(llvm::SCCPPass());
    // clean up any dead code that is missing
    FPM.addPass(llvm::DCEPass());*/

    std::cout << "Just constructed function pass manager, moving onto module pass manager" << std::endl;

    // some analysis objects required for a complete pass
    /*llvm::ModuleAnalysisManager MAM;
    llvm::FunctionAnalysisManager FAM;
    llvm::LoopAnalysisManager LAM;
    llvm::CGSCCAnalysisManager CGAM;

    // the pass manager creates the context for the pass
    llvm::PassBuilder PB;
    PB.registerModuleAnalyses(MAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);
    llvm::ModulePassManager MPM = PB.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O1);
    MPM.addPass(llvm::createModuleToFunctionPassAdaptor(std::move(FPM)));
    std::cout << "Just completed module pass manager construction, now running it." << std::endl;
    MPM.run(*M, MAM);*/
    std::cout << "Just completed module pass manager, moving on to cleaning." << std::endl;

    // useful Cyclebite routines for denoising bitcode
    if( clean ) { CleanModule(M); }
    std::cout << "Just completed cleaning, moving on to split." << std::endl;
    Split(M);
    std::cout << "Just completed splitting, moving on to annotate." << std::endl;
    // gives all values and blocks unique identifiers
    Annotate(M);
}