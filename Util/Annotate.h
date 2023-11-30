//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include "Util/Exceptions.h"
#include "Util/Print.h"
#include <filesystem>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DIBuilder.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Metadata.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Operator.h>
#include <llvm/IR/Verifier.h>
#include <map>
#include <set>
#include <spdlog/spdlog.h>
#include <unistd.h>

namespace Cyclebite::Util
{
    /// @brief Enumerate the different states of ValueID and BlockID
    ///
    /// A ValueID or BlockID can be in three different states:
    /// -2 -> Uninitialized
    /// -1 -> Artificial (injected by tik)
    enum IDState : int64_t
    {
        Uninitialized = -2,
        Artificial = -1
    };

    inline void SetBlockID(llvm::BasicBlock *BB, int64_t i)
    {
        llvm::MDNode *idNode = llvm::MDNode::get(BB->getContext(), llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(llvm::Type::getInt64Ty(BB->getContext()), (uint64_t)i)));
        BB->getFirstInsertionPt()->setMetadata("BlockID", idNode);
    }

    inline void SetValueIDs(llvm::Value *val, uint64_t &i)
    {
        if (llvm::Instruction *inst = llvm::dyn_cast<llvm::Instruction>(val))
        {
            llvm::MDNode *idNode = llvm::MDNode::get(inst->getContext(), llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(llvm::Type::getInt64Ty(inst->getContext()), i)));
            std::string metaKind = "ValueID";
            if (inst->getMetadata(metaKind) == nullptr)
            {
                inst->setMetadata("ValueID", idNode);
                i++;
            }
            else
            {
                return;
            }
            for (unsigned int j = 0; j < inst->getNumOperands(); j++)
            {
                if (auto use = llvm::dyn_cast<llvm::User>(inst->getOperand(j)))
                {
                    SetValueIDs(llvm::cast<llvm::Value>(use), i);
                }
                else if (auto arg = llvm::dyn_cast<llvm::Argument>(inst->getOperand(j)))
                {
                    SetValueIDs(llvm::cast<llvm::Value>(arg), i);
                }
            }
        }
        else if (auto gv = llvm::dyn_cast<llvm::GlobalObject>(val))
        {
            llvm::MDNode *gvNode = llvm::MDNode::get(gv->getContext(), llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(llvm::Type::getInt64Ty(gv->getContext()), i)));
            std::string metaKind = "ValueID";
            if (gv->getMetadata(metaKind) == nullptr)
            {
                gv->setMetadata("ValueID", gvNode);
                i++;
            }
            else
            {
                return;
            }

            for (unsigned int j = 0; j < gv->getNumOperands(); j++)
            {
                if (auto use = llvm::dyn_cast<llvm::User>(gv->getOperand(j)))
                {
                    SetValueIDs(llvm::cast<llvm::Value>(use), i);
                }
                else if (auto arg = llvm::dyn_cast<llvm::Argument>(gv->getOperand(j)))
                {
                    SetValueIDs(llvm::cast<llvm::Value>(arg), i);
                }
            }
        }
        else if (auto arg = llvm::dyn_cast<llvm::Argument>(val))
        {
            // find the arg index in the parent function call and append metadata to that parent (arg0->valueID)
            auto func = arg->getParent();
            int index = 0;
            bool found = false;
            for (auto j = func->arg_begin(); j != func->arg_end(); j++)
            {
                auto funcArg = llvm::cast<llvm::Argument>(j);
                if (funcArg == arg)
                {
                    found = true;
                    break;
                }
                index++;
            }
            if (!found)
            {
                return;
            }
            std::string metaKind = "ArgId" + std::to_string(index);
            if (func->getMetadata(metaKind) == nullptr)
            {
                llvm::MDNode *argNode = llvm::MDNode::get(func->getContext(), llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(llvm::Type::getInt64Ty(func->getContext()), i)));
                func->setMetadata(metaKind, argNode);
                i++;
            }
            else
            {
                // already seen this arg
                return;
            }
        }
    }

    inline void Annotate(llvm::Module& M)
    {
        static uint64_t CyclebiteIndex = 0;
        static uint64_t CyclebiteValueIndex = 0;
        for (auto& F : M)
        {
            for (auto bb = F.begin(); bb != F.end(); bb++)
            {
                if( !llvm::isa<llvm::DbgInfoIntrinsic>(bb) )
                {
                    SetBlockID(llvm::cast<llvm::BasicBlock>(bb), (int64_t)CyclebiteIndex);
                    CyclebiteIndex++;
                    for (auto ii = bb->begin(); ii != bb->end(); ii++)
                    {
                        if( !llvm::isa<llvm::DbgInfoIntrinsic>(ii) )
                        {
                            SetValueIDs(llvm::cast<llvm::Value>(ii), CyclebiteValueIndex);
                        }
                    }
                }
            }
        }
    }

    /// @brief Wipes away all debug info instructions and metadata for instructions, functions and global variables
    inline void CleanModule(llvm::Module& M)
    {
        for (auto& F : M)
        {
            for (auto &fi : F)
            {
                std::vector<llvm::Instruction *> toRemove;
                for (auto bi = fi.begin(); bi != fi.end(); bi++)
                {
                    auto v = llvm::cast<llvm::Instruction>(bi);
                    if (auto ci = llvm::dyn_cast<llvm::DbgInfoIntrinsic>(v))
                    {
                        toRemove.push_back(ci);
                    }
                    else
                    {
                        llvm::SmallVector<std::pair<unsigned, llvm::MDNode *>, 1> MDs;
                        v->getAllMetadata(MDs);
                        for (auto MD : MDs)
                        {
                            v->setMetadata(MD.first, nullptr);
                        }
                    }
                }
                for (auto r : toRemove)
                {
                    r->eraseFromParent();
                }
            }
            llvm::SmallVector<std::pair<unsigned, llvm::MDNode *>, 1> MDs;
            F.getAllMetadata(MDs);
            for (auto MD : MDs)
            {
                F.setMetadata(MD.first, nullptr);
            }
        }

        for (auto gi = M.global_begin(); gi != M.global_end(); gi++)
        {
            auto gv = llvm::cast<llvm::GlobalVariable>(gi);
            llvm::SmallVector<std::pair<unsigned, llvm::MDNode *>, 1> MDs;
            gv->getAllMetadata(MDs);
            for (auto MD : MDs)
            {
                gv->setMetadata(MD.first, nullptr);
            }
        }
    }

    inline int64_t GetBlockID(const llvm::BasicBlock *BB)
    {
        int64_t result = IDState::Uninitialized;
        if (BB->empty())
        {
            return result;
        }
        auto *first = llvm::cast<llvm::Instruction>(BB->getFirstInsertionPt());
        if (llvm::MDNode *node = first->getMetadata("BlockID"))
        {
            auto ci = llvm::cast<llvm::ConstantInt>(llvm::cast<llvm::ConstantAsMetadata>(node->getOperand(0))->getValue());
            result = ci->getSExtValue();
        }
        return result;
    }

    inline int64_t GetValueID(const llvm::Value *val)
    {
        int64_t result = IDState::Uninitialized;
        if (auto *first = llvm::dyn_cast<llvm::Instruction>(val))
        {
            if (llvm::MDNode *node = first->getMetadata("ValueID"))
            {
                auto ci = llvm::cast<llvm::ConstantInt>(llvm::cast<llvm::ConstantAsMetadata>(node->getOperand(0))->getValue());
                result = ci->getSExtValue();
            }
        }
        else if (auto second = llvm::dyn_cast<llvm::GlobalObject>(val))
        {
            if (llvm::MDNode *node = second->getMetadata("ValueID"))
            {
                auto ci = llvm::cast<llvm::ConstantInt>(llvm::cast<llvm::ConstantAsMetadata>(node->getOperand(0))->getValue());
                result = ci->getSExtValue();
            }
        }
        else if (auto third = llvm::dyn_cast<llvm::Argument>(val))
        {
            auto func = third->getParent();
            int index = 0;
            bool found = false;
            for (auto j = func->arg_begin(); j != func->arg_end(); j++)
            {
                auto funcArg = llvm::cast<llvm::Argument>(j);
                if (funcArg == third)
                {
                    found = true;
                    break;
                }
                index++;
            }
            if (!found)
            {
                return result;
            }
            std::string metaKind = "ArgId" + std::to_string(index);
            if (auto node = func->getMetadata(metaKind))
            {
                auto ci = llvm::cast<llvm::ConstantInt>(llvm::cast<llvm::ConstantAsMetadata>(node->getOperand(0))->getValue());
                result = ci->getSExtValue();
            }
        }
        return result;
    }

    inline bool isAllocatingFunction(const llvm::CallBase* call)
    {
        if( call->getCalledFunction() )
        {
            if( (call->getCalledFunction()->getName() == "malloc") || (call->getCalledFunction()->getName() == "_Znam") || (call->getCalledFunction()->getName() == "_Znwm") )
            {
                return true;
            }
            else if( call->getCalledFunction()->getName() == "calloc" )
            {
                spdlog::warn("Cannot yet support the size parameter of calloc. Allocation may be erroneously considered too small for processing.");
                return true;
            }
        }
        return false;
    }

    inline bool isFreeingFunction(const llvm::CallBase* call)
    {
        if( call->getCalledFunction() )
        {
            if( (call->getCalledFunction()->getName() == "free") || (call->getCalledFunction()->getName() == "_ZdlPv") )
            {
                return true;
            }
        }
        return false;
    }

    inline void findLine(const std::vector<std::string> &modLines, const std::string &name, unsigned int &lineNo, bool inst = false)
    {
        for (unsigned int i = lineNo; i < modLines.size(); i++)
        {
            if (inst)
            {
                if (modLines[i - 1].find("!BlockID ") != std::string::npos)
                {
                    lineNo = i;
                    return;
                }
                else if (modLines[i - 1].find("Function Attrs") != std::string::npos)
                {
                    // end of the line, return
                    lineNo = i;
                    return;
                }
            }
            else if ((modLines[i - 1].find("define") != std::string::npos) && (modLines[i - 1].find(name) != std::string::npos))
            {
                lineNo = i;
                return;
            }
        }
        // we couldn't find a definition. try again
        if (inst)
        {
            return;
        }
        lineNo = 1;
        findLine(modLines, name, lineNo);
    }

    // the exports here represent the alloca mapped to an export
    // therefore the debug information will capture the pointer operations
    inline void DebugExports(llvm::Module *mod, const std::string &fileName)
    {
        if (mod->getModuleFlag("Debug Info Version") == nullptr)
        {
            mod->addModuleFlag(llvm::Module::Warning, "Debug Info Version", llvm::DEBUG_METADATA_VERSION);
        }
        llvm::DIBuilder DBuild(*mod);
        std::string cwd = get_current_dir_name();
        auto uType = DBuild.createBasicType("export", 64, llvm::dwarf::DW_ATE_address, llvm::DINode::DIFlags::FlagArtificial);
        auto DFile = DBuild.createFile(fileName, cwd);
        DBuild.createCompileUnit(llvm::dwarf::DW_LANG_C, DFile, "clang", false, ".", 0);
        unsigned int lineNo = 1;
        std::string strDump;
        llvm::raw_string_ostream OS(strDump);
        OS << *mod;
        OS.flush();
        std::string line;
        std::vector<std::string> modLines;
        auto modStream = std::stringstream(strDump);
        while (std::getline(modStream, line, '\n'))
        {
            modLines.push_back(line);
        }
        for (auto &f : *mod)
        {
            if (f.hasExactDefinition())
            {
                /*llvm::MDNode *FMDN;
                if (f.hasMetadata("KernelName"))
                {
                    FMDN = f.getMetadata("KernelName");
                }
                else
                {
                    FMDN = llvm::MDNode::get(f.getContext(), llvm::MDString::get(f.getContext(), f.getName()));
                }*/
                std::vector<llvm::Metadata *> ElTys;
                ElTys.push_back(uType);
                for (unsigned int i = 0; i < f.getNumOperands(); i++)
                {
                    ElTys.push_back(uType);
                }
                auto ElTypeArray = DBuild.getOrCreateTypeArray(ElTys);
                auto SubTy = DBuild.createSubroutineType(ElTypeArray);
                auto FContext = DFile;
                std::string funcName = std::string(f.getName());
                findLine(modLines, funcName, lineNo);
                auto SP = DBuild.createFunction(FContext, f.getName(), llvm::StringRef(), DFile, lineNo, SubTy, lineNo, llvm::DINode::FlagZero, llvm::DISubprogram::DISPFlags::SPFlagDefinition);
                f.setSubprogram(SP);
                for (auto b = f.begin(); b != f.end(); b++)
                {
                    findLine(modLines, "", lineNo, true);
                    for (auto it = b->begin(); it != b->end(); it++)
                    {
                        if (auto inst = llvm::dyn_cast<llvm::Instruction>(it))
                        {
                            if (auto al = llvm::dyn_cast<llvm::AllocaInst>(inst))
                            {
                                std::string metaString;
                                if (it->getMetadata("ValueID") != nullptr)
                                {
                                    auto MDN = it->getMetadata("ValueID");
                                    auto ci = llvm::cast<llvm::ConstantInt>(llvm::cast<llvm::ConstantAsMetadata>(MDN->getOperand(0))->getValue());
                                    int64_t ID = ci->getSExtValue();
                                    if (ID == IDState::Artificial)
                                    {
                                        auto DL = llvm::DILocation::get(SP->getContext(), lineNo, 0, SP);
                                        modLines.insert(modLines.begin() + lineNo, "");
                                        lineNo++;
                                        auto DL2 = llvm::DILocation::get(SP->getContext(), lineNo, 0, SP);
                                        auto D = DBuild.createAutoVariable(SP, "export_" + std::to_string(lineNo), DFile, lineNo, uType);
                                        DBuild.insertDeclare(al, D, DBuild.createExpression(), DL, al);
                                        inst->setDebugLoc(DL2);
                                    }
                                }
                                else
                                {
                                    auto DL = llvm::DILocation::get(SP->getContext(), lineNo, 0, SP);
                                    al->setDebugLoc(DL);
                                }
                            }
                            else
                            {
                                auto DL = llvm::DILocation::get(SP->getContext(), lineNo, 0, SP);
                                inst->setDebugLoc(DL);
                            }
                        }
                        lineNo++;
                    }
                }
                DBuild.finalizeSubprogram(SP);
            }
            if (lineNo >= modLines.size())
            {
                lineNo = 0;
            }
        }
        DBuild.finalize();
    }

    inline void SetFunctionAnnotation(llvm::Function *F, std::string key, int64_t value)
    {
        llvm::MDNode *idNode = llvm::MDNode::get(F->getContext(), llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(llvm::Type::getInt64Ty(F->getContext()), (uint64_t)value)));
        F->setMetadata(key, idNode);
    }

    inline int64_t GetFunctionAnnotation(llvm::Function *F, std::string key)
    {
        int64_t result = -1;
        if (llvm::MDNode *node = F->getMetadata(key))
        {
            auto ci = llvm::cast<llvm::ConstantInt>(llvm::cast<llvm::ConstantAsMetadata>(node->getOperand(0))->getValue());
            result = ci->getSExtValue();
        }
        return result;
    }

    inline uint64_t GetBlockCount(const llvm::Module& M)
    {
        uint64_t result = 0;
        for (auto& F : M)
        {
            for (auto &fi : F)
            {
                if (llvm::isa<llvm::BasicBlock>(fi))
                {
                    result++;
                }
            }
        }
        return result;
    }

    inline void VerifyModule(llvm::Module& M)
    {
        std::string str;
        llvm::raw_string_ostream rso(str);
        bool broken = verifyModule(M, &rso);
        if (broken)
        {
            auto err = rso.str();
            spdlog::critical("Tik Module Corrupted: \n" + err);
        }
    }

    inline const std::vector<const llvm::Function*> GetFunctionsFromCall( const llvm::CallBase* call, 
                                                      const std::map<int64_t, std::vector<int64_t>>& blockCallers, 
                                                      const std::map<int64_t, const llvm::BasicBlock*>& IDToBlock )
    {
        std::vector<const llvm::Function*> funcs;
        if( call->getCalledFunction() )
        {
            funcs.push_back(call->getCalledFunction());
            return funcs;
        }
        else
        {
            auto instString = PrintVal(call, false);
            // blockCallers should tell us which basic block this null function call goes to next
            if (blockCallers.find(Cyclebite::Util::GetBlockID(call->getParent())) != blockCallers.end())
            {
                // this is a multi-dimensional problem, even with basic block splitting
                // a function pointer is allowed to call any function that matches a signature
                // when a function pointer goes to more than one function, we have to be able to enumerate that case here
                for (auto callee : blockCallers.at(Cyclebite::Util::GetBlockID(call->getParent())))
                {
                    funcs.push_back(IDToBlock.at(callee)->getParent());
                }
            }
            // there is a corner case here where libc functions can appear to be null when in fact they are statically determinable
            // this can happen if somebody uses a libc function but doesn't include the corresponding header (this will show up as a warning about an undeclared function)
            // the linker will make this all okay, but within the bitcode module for some reason the LLVM api will return null, even when a function pointer is not used
            // example: Algorithms/UnitTests/SimpleRecursion (fibonacci), the atoi() function will appear to be a null function call unless #include <stdlib.h> is at the top
            // the below checks will break because the function itself will appear "empty" in the llvm IR (it is from libc and we don't profile that)
            // since the function call is not profiled, we will not get an entry in blockCallers for it
            // to help prevent this, the user can pass -Werror
            // at this point, the only way I can think of to detect this case is to see if there is actually a function name (with a preceding @ symbol)
            // the above mechanism will fail if the null function call has a global variable in its arguments list (globals are preceded by @ too)
            else if (instString.find('@') != std::string::npos)
            {
                // this is likely the corner case explained above, so we skip
                // John: 4/20/22, we should keep track of this, throw a warning (to measure the nature of this phenomenon [is it just libc, how prevalent is it])
                spdlog::warn("Found a statically determinable function call that appeared to be null. This is likely caused by a lack of declaration in the original source file.");
            }
            else
            {
                // this case could be due to either an empty function being called (a function that isn't in the input bitcode module) or profiler error... There really isn't any way of us knowing at this stage
#ifdef DEBUG
                PrintVal(call->getParent());
                spdlog::warn("Blockcallers did not contain information for a null function call observed in the dynamic profile. This could be due to an empty function or profiler error.");
#endif
            }
        }
        return funcs;
    }
} // namespace Cyclebyte::Util