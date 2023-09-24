#pragma once
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

namespace Cyclebite::Profile::Passes
{
    struct Annotate : llvm::PassInfoMixin<Annotate> 
    {
        llvm::PreservedAnalyses run(llvm::Module& M, llvm::ModuleAnalysisManager& );
        // without setting this to true, all modules with "optnone" attribute are skipped
        static bool isRequired();
    };
} // namespace Cyclebite::Profile::Passes