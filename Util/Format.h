// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "Util/Annotate.h"
#include "Util/Split.h"
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/Utils.h>

inline void Format(llvm::Module *M, bool clean = true)
{
    // Ben [4/13/22]
    // useful transforms for bitcode simplification
    // all transforms need to be done to the bitcode before the Annotate pass is run
    llvm::legacy::FunctionPassManager FPM(M);
    FPM.add(llvm::createConstantPropagationPass());
    FPM.add(llvm::createIndVarSimplifyPass());
    FPM.add(llvm::createDeadCodeEliminationPass());
    FPM.add(llvm::createLoopSimplifyPass());
    FPM.doInitialization();
    for (auto f = M->begin(); f != M->end(); f++)
    {
        FPM.run(*f);
    }
    FPM.doFinalization();
    // useful Cyclebite routines for denoising bitcode
    if( clean ) { CleanModule(M); }
    Split(M);
    // gives all values and blocks unique identifiers
    Annotate(M);
}