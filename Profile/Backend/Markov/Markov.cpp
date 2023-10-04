//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "DashHashTable.h"
#include "ThreadSafeQueue.h"
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <set>

#define STACK_SIZE 0xff

using namespace std;
using json = nlohmann::json;

namespace Cyclebite::Markov
{

    constexpr uint32_t BIN_SIZE = 0xfff;
    /// @brief Facilitates an array of pointers that is used cyclically during the execution of the profile to use in the ThreadSafeQueue
    ///
    ///
    struct TaskBin
    {
        Cyclebite::Profile::Backend::EdgeInc*    edgeArray;
        Cyclebite::Profile::Backend::CallInc*    callArray;
        Cyclebite::Profile::Backend::LabelEvent* labelArray;
        // the next location to be allocated to a writer (to the ThreadSafeQueue)
        atomic<uint32_t> edge_write;
        // the next location to be freed by a reader (of the ThreadSafeQueue)
        atomic<uint32_t> edge_read;
        atomic<uint32_t> call_write;
        atomic<uint32_t> call_read;
        atomic<uint32_t> label_write;
        atomic<uint32_t> label_read;
        TaskBin()
        {
            edgeArray  = (Cyclebite::Profile::Backend::EdgeInc*)malloc( (BIN_SIZE+1)*sizeof(Cyclebite::Profile::Backend::EdgeInc) );
            callArray  = (Cyclebite::Profile::Backend::CallInc*)malloc( (BIN_SIZE+1)*sizeof(Cyclebite::Profile::Backend::CallInc) );
            labelArray = (Cyclebite::Profile::Backend::LabelEvent*)malloc( (BIN_SIZE+1)*sizeof(Cyclebite::Profile::Backend::LabelEvent) );
            edge_read = 0;
            edge_write = 0;
            call_read = 0;
            call_write = 0;
            label_read = 0;
            label_write = 0;
        }
        ~TaskBin()
        {
            free(edgeArray);
            free(callArray);
            free(labelArray);
        }
        Cyclebite::Profile::Backend::EdgeInc* getPtr(const Cyclebite::Profile::Backend::EdgeInc& inc)
        {
            auto w = edge_write++;
            edgeArray[w & BIN_SIZE] = inc;
            return &edgeArray[w & BIN_SIZE];
        }
        Cyclebite::Profile::Backend::CallInc* getPtr(const Cyclebite::Profile::Backend::CallInc& call)
        {
            auto w = call_write++;
            callArray[w & BIN_SIZE] = call;
            return &callArray[w & BIN_SIZE];
        }
        Cyclebite::Profile::Backend::LabelEvent* getPtr(const Cyclebite::Profile::Backend::LabelEvent& inc)
        {
            auto w = label_write++;
            labelArray[w & BIN_SIZE] = inc;
            return &labelArray[w & BIN_SIZE];
        }
    };

    // stack for storing labels
    char *labelStack[STACK_SIZE + 1];
    std::atomic<uint32_t> stackCount = 0;

    void pushLabelStack(char *newLabel)
    {
        auto p = stackCount++;
        labelStack[p & STACK_SIZE] = newLabel;
    }

    char *popLabelStack()
    {
        auto p = stackCount--;
        char *pop = labelStack[(p-1) & STACK_SIZE];
        return pop;
    }

    char* readLabelStack()
    {
        uint32_t r = stackCount;
        return labelStack[ (r-1) & STACK_SIZE ];
    }

    // holds the count of all blocks in the bitcode source file
    uint64_t totalBlocks;
    // Circular buffer of the previous MARKOV_ORDER blocks seen
    uint64_t b[MARKOV_ORDER];
    // Flag indicating whether the program is actively being profiled
    bool markovActive = false;
    // Hash table for the edges of the control flow graph
    __TA_HashTable *edgeHashTable;
    // Hash table for the labels of each basic block
    __TA_HashTable *labelHashTable;
    // Hash table for the caller-callee hash table
    __TA_HashTable *callerHashTable;
    // start and end points to specifically time the profiles
    struct timespec __TA_stopwatch_start;
    struct timespec __TA_stopwatch_end;

    // multithreaded components
    Cyclebite::Markov::TaskBin TB;
    Cyclebite::Profile::Backend::ThreadSafeQueue Q;
    std::thread* reader;
    // task builder, assigned to each thread 
    std::map<std::thread::id, Cyclebite::Profile::Backend::Task> taskBuffer;
    // holds new markov events
    std::map<std::thread::id, Cyclebite::Profile::Backend::EdgeInc> edgeInc;
    // holds new label events
    std::map<std::thread::id, Cyclebite::Profile::Backend::LabelEvent> labelInc;
    // holds new caller-callee events
    std::map<std::thread::id, Cyclebite::Profile::Backend::CallInc> callInc;
    // container for all basic blocks that spawn threads
    std::set<uint64_t> launchers;
    // sets the last block known to launch a thread
    uint64_t lastLauncher;
    // container for all basic blocks that are the entrance to spawned threads
    std::set<uint64_t> threadSpawns;
    // mutex to lock out all threads when the size of thread-dependent containers changes
    std::atomic<uint32_t> newThread = 0;
    // atomic to keep track of how many threads are currently using the backend
    std::atomic<uint32_t> miners = 0;

    void __TA_WriteJsonFiles(__TA_HashTable *labelHashTable, __TA_HashTable *callerHashTable, const std::set<uint64_t>& launchers, const std::set<uint64_t>& threadStarts )
    {
        // construct BlockInfo json output
        map<string, map<string, uint64_t>> labelMap;
        for (uint32_t i = 0; i < labelHashTable->getFullSize(labelHashTable); i++)
        {
            for (uint32_t j = 0; j < labelHashTable->array[i].popCount; j++)
            {
                auto entry = labelHashTable->array[i].tuple[j];
                string block = to_string(entry.label.blocks[0]);
                labelMap[block][string(entry.label.label)] = entry.label.frequency;
            }
        }
#if MARKOV_ORDER
        // to print the callermap, we have to collect all entries and group them by their caller entry
        // then we have to order those entries by their position member
        // then we have to construct the values of the map according to that order (first entry in the value vector is position 0 in the basic block)
        auto callerMap = std::map<std::string, std::vector<pair<uint32_t, uint32_t>>>(); // first - callee BBID, second - position
        for (uint32_t i = 0; i < callerHashTable->getFullSize(callerHashTable); i++)
        {
            for (uint32_t j = 0; j < callerHashTable->array[i].popCount; j++)
            {
                auto entry = callerHashTable->array[i].tuple[j];
                string label = to_string(entry.callee.blocks[0]);
                callerMap[label].push_back(pair<uint32_t, uint32_t>(entry.callee.blocks[1], entry.callee.position));
            }
        }
        json blockInfo;
        for (auto label : labelMap)
        {
            blockInfo[label.first]["Labels"] = label.second;
        }
        // representing the caller-callee data is a 2D problem
        // x -> multiple function calls for a given basic block (ie multiple callees for one caller)
        // y -> multiple functions for a given call inst (function pointers can go to different places)
        // we do away with X by splitting basic blocks at function calls (therefore, the "position" member in callee.position is not used... it is always 0)
        // we still have to deal with y
        for (auto caller : callerMap)
        {
            vector<uint32_t> callees;
            for (auto callee : caller.second)
            {
                // we don't care about position, therefore loc.first is the only value we keep
                callees.push_back(callee.first);
            }
            blockInfo[caller.first]["BlockCallers"] = callees;
        }
        if( !launchers.empty() )
        {
            blockInfo["ThreadLaunchers"] = launchers;
        }
        if( !threadStarts.empty() )
        {
            blockInfo["ThreadEntrances"] = threadStarts;
        }
#endif
        ofstream file;
        char *blockInfoFileName = getenv("BLOCK_FILE");
        if (blockInfoFileName == nullptr)
        {
            file.open("BlockInfo.json");
        }
        else
        {
            file.open(blockInfoFileName);
        }
        file << setw(4) << blockInfo;
        file.close();
    }
} // namespace Cyclebite::Markov

extern "C"
{
    void MarkovPush(Cyclebite::Profile::Backend::ThreadSafeQueue* Q, HashTable* edge, HashTable* call, HashTable* label)
    {
        while( Cyclebite::Markov::markovActive || Q->members() )
        {
            auto t = Q->pop(true);
            if( t.ID() == __LONG_MAX__ )
            {
#ifdef DEBUG
                cout << "Task read ran out of tries with queue at size " << Q->members() << "!" << endl;
#endif
                continue;
            }
            else if( t.tasks() > 6 )
            {
#ifdef DEBUG
                cout << "Just got a bad task from a pop with the queue at size " << Q->members() << "!" << endl;
#endif
                exit(EXIT_FAILURE);
            }
            auto ret = t.pushTasks(edge, call, label);
            if( ret )
            {
#ifdef DEBUG
                cout << "Error when pushing task " << t.ID() << " to the hash table." << endl;
#endif
            }
        }
    }
    void MarkovInit(uint64_t blockCount, uint64_t ID)
    {
        Cyclebite::Markov::taskBuffer[std::this_thread::get_id()] = Cyclebite::Profile::Backend::Task();
        // circular buffer initializations
        Cyclebite::Markov::edgeInc[std::this_thread::get_id()].snk = ID;
        Cyclebite::Markov::callInc[std::this_thread::get_id()] = Cyclebite::Profile::Backend::CallInc();
        Cyclebite::Markov::labelInc[std::this_thread::get_id()] = Cyclebite::Profile::Backend::LabelEvent();        
        // edge hash table
        Cyclebite::Markov::edgeHashTable = (__TA_HashTable *)malloc(sizeof(__TA_HashTable));
        Cyclebite::Markov::edgeHashTable->size = (uint32_t)(ceil(log((double)blockCount) / log(2.0)));
        Cyclebite::Markov::edgeHashTable->getFullSize = __TA_getFullSize;
        Cyclebite::Markov::edgeHashTable->array = (__TA_arrayElem *)calloc(Cyclebite::Markov::edgeHashTable->getFullSize(Cyclebite::Markov::edgeHashTable), sizeof(__TA_arrayElem));
        Cyclebite::Markov::edgeHashTable->miners = 0;
        Cyclebite::Markov::edgeHashTable->newMine = 0;
        // label hash table
        Cyclebite::Markov::labelHashTable = (__TA_HashTable *)malloc(sizeof(__TA_HashTable));
        Cyclebite::Markov::labelHashTable->size = (uint32_t)(ceil(log((double)blockCount) / log(2.0)));
        Cyclebite::Markov::labelHashTable->getFullSize = __TA_getFullSize;
        Cyclebite::Markov::labelHashTable->array = (__TA_arrayElem *)calloc(Cyclebite::Markov::labelHashTable->getFullSize(Cyclebite::Markov::labelHashTable), sizeof(__TA_arrayElem));
        Cyclebite::Markov::labelHashTable->miners = 0;
        Cyclebite::Markov::labelHashTable->newMine = 0;
        // caller hash table
        Cyclebite::Markov::callerHashTable = (__TA_HashTable *)malloc(sizeof(__TA_HashTable));
        Cyclebite::Markov::callerHashTable->size = (uint32_t)(ceil(log((double)blockCount) / log(2.0)));
        Cyclebite::Markov::callerHashTable->getFullSize = __TA_getFullSize;
        Cyclebite::Markov::callerHashTable->array = (__TA_arrayElem *)calloc(Cyclebite::Markov::callerHashTable->getFullSize(Cyclebite::Markov::callerHashTable), sizeof(__TA_arrayElem));
        Cyclebite::Markov::callerHashTable->miners = 0;
        Cyclebite::Markov::callerHashTable->newMine = 0;

        Cyclebite::Markov::totalBlocks = blockCount;
        Cyclebite::Markov::markovActive = true;
        while (clock_gettime(CLOCK_MONOTONIC, &Cyclebite::Markov::__TA_stopwatch_start))
            ;

        Cyclebite::Markov::reader = new std::thread(MarkovPush, &Cyclebite::Markov::Q, Cyclebite::Markov::edgeHashTable, Cyclebite::Markov::callerHashTable, Cyclebite::Markov::labelHashTable);
    }
    void MarkovDestroy()
    {
        // push any remaining hash table entries
        for( const auto& id : Cyclebite::Markov::taskBuffer )
        {
            if( id.second.tasks() )
            {
                // we need to push the current task and add our event to a new task
                if( !Cyclebite::Markov::Q.push(id.second, true) )
                {
#ifdef DEBUG
                    printf("Task queue push returned error code\n");
#endif
                }
            }
        }
        Cyclebite::Markov::markovActive = false;
        // stop the timer and print
        while (clock_gettime(CLOCK_MONOTONIC, &Cyclebite::Markov::__TA_stopwatch_end))
            ;
        double secdiff = (double)(Cyclebite::Markov::__TA_stopwatch_end.tv_sec - Cyclebite::Markov::__TA_stopwatch_start.tv_sec);
        double nsecdiff = ((double)(Cyclebite::Markov::__TA_stopwatch_end.tv_nsec - Cyclebite::Markov::__TA_stopwatch_start.tv_nsec)) * pow(10.0, -9.0);
        double totalTime = secdiff + nsecdiff;
        printf("\nPROFILETIME: %f\n", totalTime);

        // wait for the reader to finish its work
        Cyclebite::Markov::reader->join();
        delete Cyclebite::Markov::reader;

        // print profile bin file
        __TA_WriteEdgeHashTable(Cyclebite::Markov::edgeHashTable, (uint32_t)Cyclebite::Markov::totalBlocks);

        // write json files
        Cyclebite::Markov::__TA_WriteJsonFiles(Cyclebite::Markov::labelHashTable, Cyclebite::Markov::callerHashTable, Cyclebite::Markov::launchers, Cyclebite::Markov::threadSpawns);

        // free everything
        free(Cyclebite::Markov::edgeHashTable->array);
        free(Cyclebite::Markov::edgeHashTable);
        free(Cyclebite::Markov::labelHashTable->array);
        free(Cyclebite::Markov::labelHashTable);
        free(Cyclebite::Markov::callerHashTable->array);
        free(Cyclebite::Markov::callerHashTable);
    }
    void MarkovIncrement(uint64_t a, bool funcEntrance)
    {
        if (!Cyclebite::Markov::markovActive)
        {
            return;
        }
        while( Cyclebite::Markov::newThread )
        {
            // spin
        }
        if( Cyclebite::Markov::edgeInc.find(std::this_thread::get_id()) == Cyclebite::Markov::edgeInc.end() )
        {
            std::cout << "Number of threads seen so far is " << Cyclebite::Markov::threadSpawns.size() << std::endl;
            tryAgain:
            while( Cyclebite::Markov::newThread )
            {
                // spin
            }
            Cyclebite::Markov::newThread++;
            if( Cyclebite::Markov::newThread > 1 )
            {
                // somebody beat us to it
#ifdef DEBUG
                std::cout << "We just got beat to the punch..." << std::endl;
#endif
                // jump back to the spin lock and try again
                Cyclebite::Markov::newThread--;
                goto tryAgain;
            }
            while( Cyclebite::Markov::miners > 1 )
            {
#ifdef DEBUG
                std::cout << "Miners in the backend is " << Cyclebite::Markov::miners << std::endl;
#endif
                // spin
            }
            // we just forked from a parent thread, get the src node from that ID
            Cyclebite::Markov::threadSpawns.insert(a);
            // edgeinc update
            Cyclebite::Markov::taskBuffer[std::this_thread::get_id()] = Cyclebite::Profile::Backend::Task();
            Cyclebite::Markov::edgeInc[std::this_thread::get_id()].src = Cyclebite::Markov::lastLauncher;
            Cyclebite::Markov::edgeInc.at(std::this_thread::get_id()).snk = a;
            // labelinc update
            Cyclebite::Markov::labelInc[std::this_thread::get_id()].snk = a;
            // callinc update
            Cyclebite::Markov::callInc[std::this_thread::get_id()].src = Cyclebite::Markov::lastLauncher;
            Cyclebite::Markov::callInc.at(std::this_thread::get_id()).snk = a;
            Cyclebite::Markov::newThread--;
            Cyclebite::Markov::miners++;
        }
        else
        {
            Cyclebite::Markov::edgeInc.at(std::this_thread::get_id()).src = Cyclebite::Markov::edgeInc.at(std::this_thread::get_id()).snk;
            Cyclebite::Markov::edgeInc.at(std::this_thread::get_id()).snk = a;
            Cyclebite::Markov::miners++;
        }

        // edge hash table
        auto t = Cyclebite::Markov::TB.getPtr( Cyclebite::Markov::edgeInc.at(std::this_thread::get_id()) );
        if( !Cyclebite::Markov::taskBuffer.at(std::this_thread::get_id()).addEvent( t ) )
        {
            // we need to push the current task and add our event to a new task
            if( !Cyclebite::Markov::Q.push(Cyclebite::Markov::taskBuffer.at(std::this_thread::get_id()), true) )
            {
#ifdef DEBUG
                printf("Task queue push returned error code\n");
#endif
            }
            Cyclebite::Markov::taskBuffer.at(std::this_thread::get_id()).reset();
            Cyclebite::Markov::taskBuffer.at(std::this_thread::get_id()).addEvent( t );
        }

        // label hash table
        if (Cyclebite::Markov::stackCount > 0)
        {
            Cyclebite::Markov::labelInc.at(std::this_thread::get_id()).label = Cyclebite::Markov::readLabelStack();
            Cyclebite::Markov::labelInc.at(std::this_thread::get_id()).snk = a;
            
            auto t = Cyclebite::Markov::TB.getPtr( Cyclebite::Markov::labelInc.at(std::this_thread::get_id()) );
            if( !Cyclebite::Markov::taskBuffer.at(std::this_thread::get_id()).addEvent( t ) )
            {
                // we need to push the current task and add our event to a new task
                if( !Cyclebite::Markov::Q.push(Cyclebite::Markov::taskBuffer.at(std::this_thread::get_id()), true) )
                {
    #ifdef DEBUG
                    printf("Task queue push returned error code\n");
    #endif
                }
                Cyclebite::Markov::taskBuffer.at(std::this_thread::get_id()).reset();
                Cyclebite::Markov::taskBuffer.at(std::this_thread::get_id()).addEvent( t );
            }
        }

        // caller hash table
        if (funcEntrance)
        {
            Cyclebite::Markov::callInc.at(std::this_thread::get_id()).position = 0;
            if( Cyclebite::Markov::threadSpawns.find(a) != Cyclebite::Markov::threadSpawns.end() )
            {
                // the src of this caller edge is the last launcher
                Cyclebite::Markov::callInc.at(std::this_thread::get_id()).src = Cyclebite::Markov::lastLauncher;
            }
            else
            {
                // the src of this caller edge is the edge src node
                Cyclebite::Markov::callInc.at(std::this_thread::get_id()).src = Cyclebite::Markov::edgeInc.at(std::this_thread::get_id()).src;
            }
            Cyclebite::Markov::callInc.at(std::this_thread::get_id()).snk = a;
            
            auto t = Cyclebite::Markov::TB.getPtr( Cyclebite::Markov::callInc.at(std::this_thread::get_id()) );
            if( !Cyclebite::Markov::taskBuffer.at(std::this_thread::get_id()).addEvent( t ) )
            {
                // we need to push the current task and add our event to a new task
                if( !Cyclebite::Markov::Q.push(Cyclebite::Markov::taskBuffer.at(std::this_thread::get_id()), true) )
                {
    #ifdef DEBUG
                    printf("Task queue push returned error code\n");
    #endif
                }
                Cyclebite::Markov::taskBuffer.at(std::this_thread::get_id()).reset();
                Cyclebite::Markov::taskBuffer.at(std::this_thread::get_id()).addEvent( t );
            }
        }
        Cyclebite::Markov::miners--;
    }
    void MarkovLaunch(uint64_t a)
    {
        // stores the block that is about to launch a thread
        Cyclebite::Markov::launchers.insert(a);
        Cyclebite::Markov::lastLauncher = a;
    }
    void CyclebiteMarkovKernelEnter(char *label)
    {
        Cyclebite::Markov::pushLabelStack(label);
    }
    void CyclebiteMarkovKernelExit()
    {
        Cyclebite::Markov::popLabelStack();
    }
}
