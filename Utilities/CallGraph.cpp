// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
#include "Util/Format.h"
#include "Util/IO.h"
#include "Util/Print.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Type.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/SourceMgr.h"
#include <fstream>
#include <iomanip>
#include <map>
#include <spdlog/spdlog.h>

using namespace llvm;
using namespace std;

cl::opt<std::string> InputFilename("i", cl::desc("Specify input bitcode"), cl::value_desc("bitcode filename"), cl::Required);
cl::opt<std::string> BlockInfo("j", cl::desc("Specify BlockInfo json"), cl::value_desc("BlockInfo filename"), cl::Required);
cl::opt<std::string> OutputFilename("o", cl::desc("Specify output json"), cl::value_desc("output filename"));

int main(int argc, char **argv)
{
    cl::ParseCommandLineOptions(argc, argv);
    auto blockCallers = ReadBlockInfo(BlockInfo);
    auto SourceBitcode = ReadBitcode(InputFilename);
    if (SourceBitcode == nullptr)
    {
        return EXIT_FAILURE;
    }

    // Annotate its bitcodes and values
    Cyclebite::Util::Format(*SourceBitcode.get());

    map<int64_t, BasicBlock *> IDToBlock;
    map<int64_t, Value *> IDToValue;
    map<BasicBlock *, Function *> BlockToFPtr;
    Cyclebite::Util::InitializeIDMaps(SourceBitcode.get(), IDToBlock, IDToValue);

    // Call graph, doesn't include function pointers
    auto CG = getCallGraph(SourceBitcode.get(), blockCallers, BlockToFPtr, IDToBlock);

    nlohmann::json outputJson;
    for (const auto &node : CG)
    {
        if (node.first == nullptr)
        {
            // null nodes represent theoretical entries in the call graph, see CallGraphNode class reference
            continue;
        }
        string fname = string(node.first->getName());
        outputJson[fname] = std::vector<string>();
        for (unsigned int i = 0; i < node.second->size(); i++)
        {
            auto calledFunc = (*node.second)[i]->getFunction();
            if (calledFunc != nullptr)
            {
                outputJson[string(node.first->getName())].push_back(string(calledFunc->getName()));
            }
            // null function calls can still exist, this tool only adds functions to the callgraph
        }
    }
    ofstream oStream(OutputFilename);
    oStream << setw(4) << outputJson;
    oStream.close();
    return 0;
}