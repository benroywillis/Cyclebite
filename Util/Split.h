//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include "Util/Print.h"
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Module.h>

inline void Split(llvm::Module& M)
{
    for (auto f = M.begin(); f != M.end(); f++)
    {
        // do not re-process blocks
        llvm::Function::iterator bi = f->begin();
        while (bi != f->end())
        {
            auto ii = bi->begin();
            while( ii != bi->end() )
            {
                // if this is a callinst or invoke
                if (auto cb = llvm::dyn_cast<llvm::CallBase>(ii))
                {
                    // split the basic block before and after the callinst to isolate it as the only instruction in a basic block
                    // skip debug info
                    if (!llvm::isa<llvm::DbgInfoIntrinsic>(ii))
                    {
                        // this splits the function call from the instructions that come prior to it in the basic block
                        auto newNext = bi->splitBasicBlock(cb);
                        // invoke instruction are already the terminators in their blocks so they don't need to be split from the latter part of the basic block
                        if (!llvm::isa<llvm::InvokeInst>(cb))
                        {
                            // we also want to split the call instruction from the instructions that come after it in the basic block
                            // thus, we split again
                            auto newCB = llvm::cast<llvm::CallBase>(newNext->begin());
                            auto nxt = newCB->getNextNode();
                            newNext = newNext->splitBasicBlock(nxt);
                            bi = newNext->getIterator();
                            ii = bi->begin();
                        }
                        else
                        {
                            // in the case of an invoke, we want to start the instruction iterator after the invoke instruction
                            bi = newNext->getNextNode()->getIterator();
                            ii = bi->begin();
                        }
                        continue;
                    }
                }
                ii++;
            }
            if( ii == bi->end() )
            {
                bi++;
            }
        }
    }
}