//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "Graph/inc/IO.h"
#include "Util/Format.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/CommandLine.h"
#include <iomanip>

using namespace llvm;
using namespace std;

cl::opt<string> BitcodeFileName("i", cl::desc("Input bitcode. Must be compiled with maximum debug symbols to optimize the result of this tool."), cl::value_desc("input.bc"), cl::Required);
cl::opt<string> KernelFileName("k", cl::desc("Input kernel .json file"), cl::value_desc("kernel.json"), cl::Required);
cl::opt<string> SourceFileName("o", cl::desc("Output map file"), cl::value_desc("kernel filename"), cl::init("kernelMap.json"));

int main(int argc, char **argv)
{
    cl::ParseCommandLineOptions(argc, argv);
    LLVMContext context;
    SMDiagnostic smerror;
    std::unique_ptr<Module> SourceBitcode = parseIRFile(BitcodeFileName, smerror, context);

    // Don't clean the debug info, but format it like the rest of the tools
    Cyclebite::Util::Format(*SourceBitcode, false);
    Cyclebite::Graph::InitializeIDMaps(SourceBitcode.get());

    ifstream inputJson;
    nlohmann::json j;
    try
    {
        inputJson.open(KernelFileName);
        inputJson >> j;
        inputJson.close();
    }
    catch (exception &e)
    {
        spdlog::error("Couldn't open input json file: " + KernelFileName);
        spdlog::error(e.what());
        return EXIT_FAILURE;
    }

    nlohmann::json kernelMap;
    for (auto &[key, value] : j["Kernels"].items())
    {
        map<string, set<int>> sourceLines;
        for (const auto &BID : value["Blocks"])
        {
            auto block = Cyclebite::Graph::IDToBlock[BID];
            for (auto i = block->begin(); i != block->end(); i++ )
            {
                if (i->hasMetadata())
                {
                    const auto &LOC = i->getDebugLoc();
                    if (LOC.getAsMDNode())
                    {
                        if (auto scope = dyn_cast<DIScope>(LOC.getScope()))
                        {
                            string dir = string(scope->getFile()->getDirectory());
                            dir.append("/");
                            dir.append(scope->getFile()->getFilename());
                            sourceLines[dir].insert(LOC.getLine());
                        }
                    }
                }
            }
        }
        kernelMap["Kernels"][key] = sourceLines;
    }
    // add a section mapping the basic block IDs to a source code line
    //kernelMap["Blocks"] = map<int64_t, map<string, set<int>>>();
    for (auto f = SourceBitcode->begin(); f != SourceBitcode->end(); f++)
    {
        for (auto b = f->begin(); b != f->end(); b++)
        {
            if (b->getFirstInsertionPt()->hasMetadata())
            {
                const auto &LOC = b->getFirstInsertionPt()->getDebugLoc();
                if (LOC.getAsMDNode() != nullptr)
                {
                    if (auto scope = dyn_cast<DIScope>(LOC.getScope()))
                    {
                        string dir = string(scope->getFile()->getDirectory());
                        dir.append("/");
                        dir.append(scope->getFile()->getFilename());
                        kernelMap["Blocks"][to_string(Cyclebite::Util::GetBlockID(cast<BasicBlock>(b)))][dir].push_back(LOC.getLine());
                    }
                }
            }
        }
    }

    std::ofstream file;
    file.open(SourceFileName);
    file << setw(4) << kernelMap;
    file.close();
    return 0;
}