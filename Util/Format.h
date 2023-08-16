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

inline void Format(llvm::Module *M, bool clean = true)
{
    // Ben [4/13/22]
    // useful transforms for bitcode simplification
    // all transforms need to be done to the bitcode before the Annotate pass is run
    llvm::FunctionPassManager FPM;
    // induction variable simplification
    FPM.addPass(llvm::createFunctionToLoopPassAdaptor(llvm::IndVarSimplifyPass()));
    // transforms loops to simplest form llvm knows of, defined at https://llvm.org/docs/LoopTerminology.html
    FPM.addPass(llvm::LoopSimplifyPass());
    // constant propogation/dead code elimination pass
    FPM.addPass(llvm::SCCPPass());
    // clean up any dead code that is missing
    FPM.addPass(llvm::DCEPass());

    llvm::ModulePassManager MPM;
    MPM.addPass(llvm::createModuleToFunctionPassAdaptor(std::move(FPM)));
    llvm::ModuleAnalysisManager MAM;
    MPM.run(*M, MAM);

    // useful Cyclebite routines for denoising bitcode
    if( clean ) { CleanModule(M); }
    Split(M);
    // gives all values and blocks unique identifiers
    Annotate(M);
}