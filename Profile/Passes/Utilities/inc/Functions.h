//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#pragma once
#include <llvm/IR/Function.h>

using namespace llvm;

namespace Cyclebite::Profile::Passes
{
    extern Function *openFunc;
    extern Function *closeFunc;
    extern Function *BB_ID;
    extern Function *StoreDump;
    extern Function *DumpStoreValue;
    extern Function *LoadDump;
    extern Function *DumpLoadValue;
    extern Function *fullFunc;
    extern Function *fullAddrFunc;
    // Markov pass
    extern Function *MarkovOpen;
    extern Function *MarkovClose;
    extern Function *MarkovInit;
    extern Function *MarkovDestroy;
    extern Function *MarkovIncrement;
    extern Function *MarkovReturn;
    extern Function *MarkovExit;
    extern Function *MarkovLaunch;
    // Timing pass
    extern Function *TimingInit;
    extern Function *TimingDestroy;
    // Instance pass
    extern Function *InstanceInit;
    extern Function *InstanceDestroy;
    extern Function *InstanceIncrement;
    // LastWriter pass
    extern Function *LastWriterLoad;
    extern Function *LastWriterStore;
    extern Function *LastWriterIncrement;
    extern Function *LastWriterInitialization;
    extern Function *LastWriterDestroy;
    // LL Memprofile pass
    extern Function *MemProfInitialization;
    extern Function *MemProfDestroy;
    extern Function *LoadInstructionDump;
    extern Function *StoreInstructionDump;
    // Epoch pass
    extern Function *MemoryLoad;
    extern Function *MemoryStore;
    extern Function *MemoryIncrement;
    extern Function *MemoryInit;
    extern Function *MemoryDestroy;
    extern Function *MemoryCpy;
    extern Function *MemoryMov;
    extern Function *MemorySet;
    extern Function *MemoryMalloc;
    extern Function *MemoryFree;
    // Precision pass
    extern Function *PrecisionIncrement;
    extern Function *PrecisionLoad;
    extern Function *PrecisionStore;
    extern Function *PrecisionInit;
    extern Function *PrecisionDestroy;
} // namespace Cyclebite::Profile::Passes
