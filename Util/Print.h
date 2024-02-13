//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include "MLCycle.h"
#include "UnconditionalEdge.h"
#include <exception>
#include <fstream>
#include <iostream>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/IR/AssemblyAnnotationWriter.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/Support/raw_ostream.h>
#include <spdlog/spdlog.h>

void DebugExports(llvm::Module *, const std::string &);
namespace Cyclebite::Util
{
    inline std::string PrintVal(const llvm::Value *val, bool print = true)
    {
        std::string str;
        llvm::raw_string_ostream rso(str);
        val->print(rso);
        if (print)
        {
            std::cout << str << "\n";
        }
        return str;
    }

    inline void PrintVal(const llvm::Metadata *val)
    {
        std::string str;
        llvm::raw_string_ostream rso(str);
        val->print(rso);
        std::cout << str << "\n";
    }

    inline void PrintVal(const llvm::NamedMDNode *val)
    {
        std::string str;
        llvm::raw_string_ostream rso(str);
        val->print(rso);
        std::cout << str << "\n";
    }

    inline void PrintVal(const llvm::Module *mod)
    {
        llvm::AssemblyAnnotationWriter *write = new llvm::AssemblyAnnotationWriter();
        std::string str;
        llvm::raw_string_ostream rso(str);
        mod->print(rso, write);
        std::cout << str << "\n";
    }

    inline std::string PrintVal(const llvm::Type *val, bool print = true)
    {
        std::string str;
        llvm::raw_string_ostream rso(str);
        val->print(rso);
        if( print )
        {
            std::cout << str << std::endl;
        }
        return str;
    }

    inline int PrintFile(llvm::Module *M, const std::string &file, bool ASCIIFormat, bool Debug)
    {
        try
        {
            if (ASCIIFormat || Debug)
            {
                // print human readable tik module to file
                auto *write = new llvm::AssemblyAnnotationWriter();
                std::string str;
                llvm::raw_string_ostream rso(str);
                std::filebuf f0;
                f0.open(file, std::ios::out);
                M->print(rso, write);
                std::ostream readableStream(&f0);
                readableStream << str;
                f0.close();
                if (Debug)
                {
                    DebugExports(M, file);
                    spdlog::info("Successfully injected debug symbols into bitcode.");
                    f0.open(file, std::ios::out);
                    std::string str2;
                    llvm::raw_string_ostream rso2(str2);
                    M->print(rso2, write);
                    std::ostream final(&f0);
                    final << str2;
                    f0.close();
                }
            }
            else
            {
                // non-human readable IR
                std::filebuf f;
                f.open(file, std::ios::out);
                std::ostream rawStream(&f);
                llvm::raw_os_ostream raw_stream(rawStream);
                WriteBitcodeToFile(*M, raw_stream);
            }
            spdlog::info("Successfully wrote bitcode to file");
        }
        catch (std::exception &e)
        {
            spdlog::critical("Failed to open output file: " + file + ":\n" + e.what() + "\n");
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }

    inline void PrintGraph(const std::set<Cyclebite::Graph::ControlNode *, Cyclebite::Graph::p_GNCompare> &nodes)
    {
        for (const auto &node : nodes)
        {
            spdlog::info("Examining node " + std::to_string(node->ID()));
            if (auto VKN = dynamic_cast<Cyclebite::Graph::MLCycle *>(node))
            {
                spdlog::info("This node is a virtual kernel pointing to ID " + std::to_string(VKN->KID));
            }
            std::string originalBlocks;
            for (const auto &ob : node->originalBlocks)
            {
                originalBlocks += std::to_string(ob) + ",";
            }
            spdlog::info("This node was generated from original blocks " + originalBlocks);
            std::string blocks;
            for (const auto &b : node->blocks)
            {
                blocks += std::to_string(b) + ",";
            }
            spdlog::info("This node contains blocks: " + blocks);
            std::string preds;
            for (auto pred : node->getPredecessors())
            {
                preds += std::to_string(pred->getSrc()->ID());
                if (pred != *prev(node->getPredecessors().end()))
                {
                    preds += ",";
                }
            }
            spdlog::info("Predecessors: " + preds);
            for (const auto &neighbor : node->getSuccessors())
            {
                spdlog::info("Neighbor " + std::to_string(neighbor->getSnk()->ID()) + " has instance count " + std::to_string(neighbor->getFreq()) + " and probability " + std::to_string(neighbor->getWeight()));
            }
            std::cout << std::endl;
        }
    }
} // namespace Cyclebite::Util