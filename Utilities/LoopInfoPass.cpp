// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
#include "Util/Format.h"
#include <fstream>
#include <iostream>
// new pass manager
#include <llvm/Analysis/LoopInfo.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Passes/PassBuilder.h>
// legacy pass manager
#include <llvm/Analysis/LoopPass.h>
#include <llvm/IR/LegacyPassManager.h>

#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/SourceMgr.h>
#include <nlohmann/json.hpp>
#include <string>

using namespace std;
using namespace llvm;

cl::opt<string> JsonFile("k", cl::desc("Specify input kernel json filename"), cl::value_desc("kernel filename"));
cl::opt<string> InputFile("b", cl::desc("Specify input bitcode filename"), cl::value_desc("bitcode filename"));
cl::opt<string> OutputFile("o", cl::desc("Specify output json filename"), cl::value_desc("json filename"));

int main(int argc, char *argv[])
{
    cl::ParseCommandLineOptions(argc, argv);
    /*ifstream inputJson(JsonFile);
    nlohmann::json j;
    inputJson >> j;
    inputJson.close();

    map<string, vector<int64_t>> kernels;

    for (auto &[k, l] : j["Kernels"].items())
    {
        string index = k;
        nlohmann::json kernel = l["Blocks"];
        kernels[index] = kernel.get<vector<int64_t>>();
    }*/

    //load the llvm file
    LLVMContext context;
    SMDiagnostic smerror;
    auto sourceBitcode = parseIRFile(InputFile, smerror, context);
    Cyclebite::Util::Format(*sourceBitcode);

    // run loop analysis pass
    /* this stuff uses the new pass manager and segfaults when main returns
    PassBuilder PB;
    ModuleAnalysisManager MAM;
    FunctionAnalysisManager FAM;
    LoopAnalysisManager LAM;
    CGSCCAnalysisManager CGAM;
    FAM.registerPass([&] { return PB.buildDefaultAAPipeline(); });

    PB.registerModuleAnalyses(MAM);
    PB.registerLoopAnalyses(LAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    ModulePassManager MPM = PB.buildPerModuleDefaultPipeline(PassBuilder::OptimizationLevel::O0);
    MPM.run(*sourceBitcode, MAM);
    */

    /*
    llvm::legacy::FunctionPassManager FPM(sourceBitcode.get());
    FPM.add(llvm::createConstantPropagationPass());
    FPM.add(llvm::createIndVarSimplifyPass());
    FPM.add(llvm::createDeadCodeEliminationPass());
    FPM.add(llvm::createLoopSimplifyPass());
    FPM.doInitialization();
    for( auto f = sourceBitcode->begin(); f != sourceBitcode->end(); f++ )
    {
        FPM.run(*f);
    }
    FPM.doFinalization();
    */

    // this is the legacy pass manager
    LPPassManager LPM;

    llvm::legacy::PassManager PM;
    PM.add(LPM.getAsPass());
    PM.run(*sourceBitcode);

    for (auto &f : *sourceBitcode)
    {
        cout << string(f.getName()) << endl;
    }

    //nlohmann::json finalJson = kernelParents;
    //ofstream oStream(OutputFile);
    //oStream << finalJson;
    //oStream.close();
    return 0;
}