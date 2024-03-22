//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "Functions.h"
#include <llvm/IR/Function.h>
using namespace llvm;

namespace Cyclebite::Profile::Passes
{
    Function *openFunc;
    Function *closeFunc;
    Function *BB_ID;
    Function *StoreDump;
    Function *DumpStoreValue;
    Function *LoadDump;
    Function *DumpLoadValue;
    Function *fullFunc;
    Function *fullAddrFunc;
    // markov pass
    Function *MarkovOpen;
    Function *MarkovClose;
    Function *MarkovInit;
    Function *MarkovDestroy;
    Function *MarkovIncrement;
    Function *MarkovReturn;
    Function *MarkovExit;
    Function *MarkovLaunch;
    // timing pass
    Function *TimingInit;
    Function *TimingDestroy;
    // instance pass
    Function *InstanceInit;
    Function *InstanceDestroy;
    Function *InstanceIncrement;
    // LastWriter pass
    Function *LastWriterLoad;
    Function *LastWriterStore;
    Function *LastWriterIncrement;
    Function *LastWriterInitialization;
    Function *LastWriterDestroy;
    // LL Memprofile pass
    Function *MemProfInitialization;
    Function *MemProfDestroy;
    Function *LoadInstructionDump;
    Function *StoreInstructionDump;
    // Epoch pass
    Function *MemoryLoad;
    Function *MemoryStore;
    Function *MemoryIncrement;
    Function *MemoryInit;
    Function *MemoryDestroy;
    Function *MemoryCpy;
    Function *MemoryMov;
    Function *MemorySet;
    Function *MemoryMalloc;
    Function *MemoryFree;
    Function *StaticBasePointer;
    // Precision pass
    Function *PrecisionIncrement;
    Function *PrecisionLoad;
    Function *PrecisionStore;
    Function *PrecisionInit;
    Function *PrecisionDestroy;
} // namespace Cyclebite::Profile::Passes