#pragma once
#include "Util/Format.h"
#include "Util/Print.h"
#include "llvm/Analysis/CallGraph.h"
#include <cmath>
#include <ctime>
#include <fstream>
#include <llvm/IR/Module.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/SourceMgr.h>
#include <memory>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <sstream>
#include <string>
#include <vector>

static llvm::LLVMContext context;
static llvm::SMDiagnostic smerror;

inline std::unique_ptr<llvm::Module> ReadBitcode(const std::string &InputFilename)
{
    std::unique_ptr<llvm::Module> SourceBitcode = parseIRFile(InputFilename, smerror, context);
    if (SourceBitcode.get() == nullptr)
    {
        spdlog::critical("Failed to open bitcode file: " + InputFilename);
    }
    Format(SourceBitcode.get());
    return SourceBitcode;
}

inline std::vector<std::unique_ptr<llvm::Module>> LoadBitcodes(const std::vector<std::string> &paths)
{
    std::vector<std::unique_ptr<llvm::Module>> result;
    for (const auto &path : paths)
    {
        result.push_back(ReadBitcode(path));
    }
    return result;
}

inline std::map<int64_t, std::vector<int64_t>> ReadBlockInfo(std::string &BlockInfo)
{
    std::map<int64_t, std::vector<int64_t>> blockCallers;
    std::ifstream inputJson;
    nlohmann::json j;
    try
    {
        inputJson.open(BlockInfo);
        inputJson >> j;
        inputJson.close();
    }
    catch (std::exception &e)
    {
        spdlog::error("Couldn't open BlockInfo json file: " + BlockInfo);
        spdlog::error(e.what());
        return blockCallers;
    }
    for (const auto &bbid : j.items())
    {
        if (j[bbid.key()].find("BlockCallers") != j[bbid.key()].end())
        {
            blockCallers[stol(bbid.key())] = j[bbid.key()]["BlockCallers"].get<std::vector<int64_t>>();
        }
    }
    return blockCallers;
}

inline std::map<int64_t, std::map<std::string, int64_t>> ReadBlockLabels(std::string &BlockInfo)
{
    std::map<int64_t, std::map<std::string, int64_t>> blockLabels;
    std::ifstream inputJson;
    nlohmann::json j;
    try
    {
        inputJson.open(BlockInfo);
        inputJson >> j;
        inputJson.close();
    }
    catch (std::exception &e)
    {
        spdlog::error("Couldn't open BlockInfo json file: " + BlockInfo);
        spdlog::error(e.what());
        return blockLabels;
    }
    for (const auto &bbid : j.items())
    {
        if (j[bbid.key()].find("Labels") != j[bbid.key()].end())
        {
            auto labelCounts = j[bbid.key()]["Labels"].get<std::map<std::string, int64_t>>();
            blockLabels[stol(bbid.key())] = labelCounts;
        }
    }
    return blockLabels;
}

inline std::set<int64_t> ReadThreadStarts(std::string &BlockInfo)
{
    std::set<int64_t> threadStarts;
    std::ifstream inputJson;
    nlohmann::json j;
    try
    {
        inputJson.open(BlockInfo);
        inputJson >> j;
        inputJson.close();
    }
    catch (std::exception &e)
    {
        spdlog::error("Couldn't open BlockInfo json file: " + BlockInfo);
        spdlog::error(e.what());
        return threadStarts;
    }
    if( j.find("ThreadEntrances") != j.end() )
    {
        for( const auto& id : j["ThreadEntrances"].get<std::vector<int64_t>>() )
        {
            threadStarts.insert(id);
        }
    }
    return threadStarts;
}

inline llvm::CallGraph getCallGraph(llvm::Module *mod, std::map<int64_t, std::vector<int64_t>> &blockCallers, std::map<llvm::BasicBlock *, llvm::Function *> &BlockToFPtr, std::map<int64_t, llvm::BasicBlock *> &IDToBlock)
{
    // this constructor does suboptimal things
    // when a function is declared and not intrinsic to the module, the constructor will put a nullptr in for the entry
    // this means that every dynamically linked function (like libc calls) are nullptrs in the callgraph
    llvm::CallGraph CG(*mod);
    // Add function pointers
    for (auto &f : *mod)
    {
        for (auto bb = f.begin(); bb != f.end(); bb++)
        {
            for (auto it = bb->begin(); it != bb->end(); it++)
            {
                // indexer to track which function call we are on in a given basic block
                // blockCallers contains in each value the sequence of blocks that are jumped to in the context switch
                // thus, callCount indexes the vector this block maps to in the blockCaller map, if any
                uint32_t callCount = 0;
                if (auto CI = llvm::dyn_cast<llvm::CallBase>(it))
                {
                    // this is supposed to detect null function calls
                    // but it's possible that libc calls will also return nullptr when getCalledFunction() is called, even if the function is statically determinable
                    // to add a second check, we look for an @ in the call instruction (because this is used for function objects in LLVM IR syntax)
                    auto callee = CI->getCalledFunction();
                    if (callee == nullptr)
                    {
                        // try to find a block caller entry for this function, if it's not there we have to move on
                        auto BBID = GetBlockID(llvm::cast<llvm::BasicBlock>(bb));
                        if (blockCallers.find(BBID) != blockCallers.end())
                        {
                            for (auto entry : blockCallers.at(BBID))
                            {
                                auto calleeBlock = IDToBlock[entry];
                                if (calleeBlock != nullptr)
                                {
                                    auto parentNode = CG.getOrInsertFunction(bb->getParent());
                                    auto childNode = CG.getOrInsertFunction(calleeBlock->getParent());
                                    parentNode->addCalledFunction(CI, childNode);
                                    BlockToFPtr[llvm::cast<llvm::BasicBlock>(bb)] = calleeBlock->getParent();
                                }
                                else
                                {
                                    throw AtlasException("Could not map a callee ID in blockCallers to a basic block!");
                                }
                            }
                        }
                        else
                        {
                            spdlog::warn("BlockCallers did not contain an entry for the indirect call in BBID " + std::to_string(BBID));
                        }
                    }
                    callCount++;
                }
            }
        }
    }
    return CG;
}

inline double CalculateTime(struct timespec *start, struct timespec *end)
{
    double time_s = (double)end->tv_sec - (double)start->tv_sec;
    double time_ns = ((double)end->tv_nsec - (double)start->tv_nsec) * pow(10.0, -9.0);
    return time_s + time_ns;
}