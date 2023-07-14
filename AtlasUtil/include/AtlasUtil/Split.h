#pragma once
#include "AtlasUtil/Print.h"
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Module.h>

inline void Split(llvm::Module *M)
{
    for (auto f = M->begin(); f != M->end(); f++)
    {
        llvm::Function::iterator bi = f->begin();
        while (bi != f->end())
        {
            for (auto ii = bi->begin(); ii != bi->end(); ii++)
            {
                // if this is a callinst or invoke
                if (auto cb = llvm::dyn_cast<llvm::CallBase>(ii))
                {
                    // split the basic block before and after the callinst to isolate it as the only instruction in a basic block
                    // skip debug info
                    if (!llvm::isa<llvm::DbgInfoIntrinsic>(ii))
                    {
                        // determines which block iterator will be used after the transformations below
                        // it is one behind because the iterator gets incremented after the instruction loop is broken
                        llvm::Function::iterator nextbi = bi;
                        // this splits the function from any prior instructions
                        auto newNext = bi->splitBasicBlock(cb);
                        // invoke instruction are already the terminators in their blocks so they don't need to be split from the rest of other functions that may be in the block
                        if (!llvm::isa<llvm::InvokeInst>(cb))
                        {
                            // getNextNode retrieves the next instruction in the block
                            auto newCB = llvm::cast<llvm::CallBase>(newNext->begin());
                            auto nxt = newCB->getNextNode();
                            newNext->splitBasicBlock(nxt);
                            nextbi = newNext->getIterator();
                        }
                        bi = newNext->getIterator();
                        break;
                    }
                }
            }
            bi++;
        }
    }
}