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

namespace TraceAtlas::Markov
{

    constexpr uint32_t BIN_SIZE = 0xfff;
    /// @brief Facilitates an array of pointers that is used cyclically during the execution of the profile to use in the ThreadSafeQueue
    ///
    ///
    struct TaskBin
    {
        TraceAtlas::Profile::Backend::EdgeInc*    edgeArray;
        TraceAtlas::Profile::Backend::CallInc*    callArray;
        TraceAtlas::Profile::Backend::LabelEvent* labelArray;
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
            edgeArray  = (TraceAtlas::Profile::Backend::EdgeInc*)malloc( (BIN_SIZE+1)*sizeof(TraceAtlas::Profile::Backend::EdgeInc) );
            callArray  = (TraceAtlas::Profile::Backend::CallInc*)malloc( (BIN_SIZE+1)*sizeof(TraceAtlas::Profile::Backend::CallInc) );
            labelArray = (TraceAtlas::Profile::Backend::LabelEvent*)malloc( (BIN_SIZE+1)*sizeof(TraceAtlas::Profile::Backend::LabelEvent) );
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
        TraceAtlas::Profile::Backend::EdgeInc* getPtr(const TraceAtlas::Profile::Backend::EdgeInc& inc)
        {
            auto w = edge_write++;
            edgeArray[w & BIN_SIZE] = inc;
            return &edgeArray[w & BIN_SIZE];
        }
        TraceAtlas::Profile::Backend::CallInc* getPtr(const TraceAtlas::Profile::Backend::CallInc& call)
        {
            auto w = call_write++;
            callArray[w & BIN_SIZE] = call;
            return &callArray[w & BIN_SIZE];
        }
        TraceAtlas::Profile::Backend::LabelEvent* getPtr(const TraceAtlas::Profile::Backend::LabelEvent& inc)
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
    TraceAtlas::Markov::TaskBin TB;
    TraceAtlas::Profile::Backend::ThreadSafeQueue Q;
    std::thread* reader;
    // task builder, assigned to each thread 
    std::map<std::thread::id, TraceAtlas::Profile::Backend::Task> taskBuffer;
    // holds new markov events
    std::map<std::thread::id, TraceAtlas::Profile::Backend::EdgeInc> edgeInc;
    // holds new label events
    std::map<std::thread::id, TraceAtlas::Profile::Backend::LabelEvent> labelInc;
    // holds new caller-callee events
    std::map<std::thread::id, TraceAtlas::Profile::Backend::CallInc> callInc;
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
} // namespace TraceAtlas::Markov

extern "C"
{
    void MarkovPush(TraceAtlas::Profile::Backend::ThreadSafeQueue* Q, HashTable* edge, HashTable* call, HashTable* label)
    {
        while( TraceAtlas::Markov::markovActive || Q->members() )
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
        TraceAtlas::Markov::taskBuffer[std::this_thread::get_id()] = TraceAtlas::Profile::Backend::Task();
        // circular buffer initializations
        TraceAtlas::Markov::edgeInc[std::this_thread::get_id()].snk = ID;
        TraceAtlas::Markov::callInc[std::this_thread::get_id()] = TraceAtlas::Profile::Backend::CallInc();
        TraceAtlas::Markov::labelInc[std::this_thread::get_id()] = TraceAtlas::Profile::Backend::LabelEvent();        
        // edge hash table
        TraceAtlas::Markov::edgeHashTable = (__TA_HashTable *)malloc(sizeof(__TA_HashTable));
        TraceAtlas::Markov::edgeHashTable->size = (uint32_t)(ceil(log((double)blockCount) / log(2.0)));
        TraceAtlas::Markov::edgeHashTable->getFullSize = __TA_getFullSize;
        TraceAtlas::Markov::edgeHashTable->array = (__TA_arrayElem *)calloc(TraceAtlas::Markov::edgeHashTable->getFullSize(TraceAtlas::Markov::edgeHashTable), sizeof(__TA_arrayElem));
        TraceAtlas::Markov::edgeHashTable->miners = 0;
        TraceAtlas::Markov::edgeHashTable->newMine = 0;
        // label hash table
        TraceAtlas::Markov::labelHashTable = (__TA_HashTable *)malloc(sizeof(__TA_HashTable));
        TraceAtlas::Markov::labelHashTable->size = (uint32_t)(ceil(log((double)blockCount) / log(2.0)));
        TraceAtlas::Markov::labelHashTable->getFullSize = __TA_getFullSize;
        TraceAtlas::Markov::labelHashTable->array = (__TA_arrayElem *)calloc(TraceAtlas::Markov::labelHashTable->getFullSize(TraceAtlas::Markov::labelHashTable), sizeof(__TA_arrayElem));
        TraceAtlas::Markov::labelHashTable->miners = 0;
        TraceAtlas::Markov::labelHashTable->newMine = 0;
        // caller hash table
        TraceAtlas::Markov::callerHashTable = (__TA_HashTable *)malloc(sizeof(__TA_HashTable));
        TraceAtlas::Markov::callerHashTable->size = (uint32_t)(ceil(log((double)blockCount) / log(2.0)));
        TraceAtlas::Markov::callerHashTable->getFullSize = __TA_getFullSize;
        TraceAtlas::Markov::callerHashTable->array = (__TA_arrayElem *)calloc(TraceAtlas::Markov::callerHashTable->getFullSize(TraceAtlas::Markov::callerHashTable), sizeof(__TA_arrayElem));
        TraceAtlas::Markov::callerHashTable->miners = 0;
        TraceAtlas::Markov::callerHashTable->newMine = 0;

        TraceAtlas::Markov::totalBlocks = blockCount;
        TraceAtlas::Markov::markovActive = true;
        while (clock_gettime(CLOCK_MONOTONIC, &TraceAtlas::Markov::__TA_stopwatch_start))
            ;

        TraceAtlas::Markov::reader = new std::thread(MarkovPush, &TraceAtlas::Markov::Q, TraceAtlas::Markov::edgeHashTable, TraceAtlas::Markov::callerHashTable, TraceAtlas::Markov::labelHashTable);
    }
    void MarkovDestroy()
    {
        // push any remaining hash table entries
        for( const auto& id : TraceAtlas::Markov::taskBuffer )
        {
            if( id.second.tasks() )
            {
                // we need to push the current task and add our event to a new task
                if( !TraceAtlas::Markov::Q.push(id.second, true) )
                {
#ifdef DEBUG
                    printf("Task queue push returned error code\n");
#endif
                }
            }
        }
        TraceAtlas::Markov::markovActive = false;
        // stop the timer and print
        while (clock_gettime(CLOCK_MONOTONIC, &TraceAtlas::Markov::__TA_stopwatch_end))
            ;
        double secdiff = (double)(TraceAtlas::Markov::__TA_stopwatch_end.tv_sec - TraceAtlas::Markov::__TA_stopwatch_start.tv_sec);
        double nsecdiff = ((double)(TraceAtlas::Markov::__TA_stopwatch_end.tv_nsec - TraceAtlas::Markov::__TA_stopwatch_start.tv_nsec)) * pow(10.0, -9.0);
        double totalTime = secdiff + nsecdiff;
        printf("\nPROFILETIME: %f\n", totalTime);

        // wait for the reader to finish its work
        TraceAtlas::Markov::reader->join();
        delete TraceAtlas::Markov::reader;

        // print profile bin file
        __TA_WriteEdgeHashTable(TraceAtlas::Markov::edgeHashTable, (uint32_t)TraceAtlas::Markov::totalBlocks);

        // write json files
        TraceAtlas::Markov::__TA_WriteJsonFiles(TraceAtlas::Markov::labelHashTable, TraceAtlas::Markov::callerHashTable, TraceAtlas::Markov::launchers, TraceAtlas::Markov::threadSpawns);

        // free everything
        free(TraceAtlas::Markov::edgeHashTable->array);
        free(TraceAtlas::Markov::edgeHashTable);
        free(TraceAtlas::Markov::labelHashTable->array);
        free(TraceAtlas::Markov::labelHashTable);
        free(TraceAtlas::Markov::callerHashTable->array);
        free(TraceAtlas::Markov::callerHashTable);
    }
    void MarkovIncrement(uint64_t a, bool funcEntrance)
    {
        if (!TraceAtlas::Markov::markovActive)
        {
            return;
        }
        while( TraceAtlas::Markov::newThread )
        {
            // spin
        }
        if( TraceAtlas::Markov::edgeInc.find(std::this_thread::get_id()) == TraceAtlas::Markov::edgeInc.end() )
        {
            std::cout << "Number of threads seen so far is " << TraceAtlas::Markov::threadSpawns.size() << std::endl;
            tryAgain:
            while( TraceAtlas::Markov::newThread )
            {
                // spin
            }
            TraceAtlas::Markov::newThread++;
            if( TraceAtlas::Markov::newThread > 1 )
            {
                // somebody beat us to it
#ifdef DEBUG
                std::cout << "We just got beat to the punch..." << std::endl;
#endif
                // jump back to the spin lock and try again
                TraceAtlas::Markov::newThread--;
                goto tryAgain;
            }
            while( TraceAtlas::Markov::miners > 1 )
            {
#ifdef DEBUG
                std::cout << "Miners in the backend is " << TraceAtlas::Markov::miners << std::endl;
#endif
                // spin
            }
            // we just forked from a parent thread, get the src node from that ID
            TraceAtlas::Markov::threadSpawns.insert(a);
            // edgeinc update
            TraceAtlas::Markov::taskBuffer[std::this_thread::get_id()] = TraceAtlas::Profile::Backend::Task();
            TraceAtlas::Markov::edgeInc[std::this_thread::get_id()].src = TraceAtlas::Markov::lastLauncher;
            TraceAtlas::Markov::edgeInc.at(std::this_thread::get_id()).snk = a;
            // labelinc update
            TraceAtlas::Markov::labelInc[std::this_thread::get_id()].snk = a;
            // callinc update
            TraceAtlas::Markov::callInc[std::this_thread::get_id()].src = TraceAtlas::Markov::lastLauncher;
            TraceAtlas::Markov::callInc.at(std::this_thread::get_id()).snk = a;
            TraceAtlas::Markov::newThread--;
            TraceAtlas::Markov::miners++;
        }
        else
        {
            TraceAtlas::Markov::edgeInc.at(std::this_thread::get_id()).src = TraceAtlas::Markov::edgeInc.at(std::this_thread::get_id()).snk;
            TraceAtlas::Markov::edgeInc.at(std::this_thread::get_id()).snk = a;
            TraceAtlas::Markov::miners++;
        }

        // edge hash table
        auto t = TraceAtlas::Markov::TB.getPtr( TraceAtlas::Markov::edgeInc.at(std::this_thread::get_id()) );
        if( !TraceAtlas::Markov::taskBuffer.at(std::this_thread::get_id()).addEvent( t ) )
        {
            // we need to push the current task and add our event to a new task
            if( !TraceAtlas::Markov::Q.push(TraceAtlas::Markov::taskBuffer.at(std::this_thread::get_id()), true) )
            {
#ifdef DEBUG
                printf("Task queue push returned error code\n");
#endif
            }
            TraceAtlas::Markov::taskBuffer.at(std::this_thread::get_id()).reset();
            TraceAtlas::Markov::taskBuffer.at(std::this_thread::get_id()).addEvent( t );
        }

        // label hash table
        if (TraceAtlas::Markov::stackCount > 0)
        {
            TraceAtlas::Markov::labelInc.at(std::this_thread::get_id()).label = TraceAtlas::Markov::readLabelStack();
            TraceAtlas::Markov::labelInc.at(std::this_thread::get_id()).snk = a;
            
            auto t = TraceAtlas::Markov::TB.getPtr( TraceAtlas::Markov::labelInc.at(std::this_thread::get_id()) );
            if( !TraceAtlas::Markov::taskBuffer.at(std::this_thread::get_id()).addEvent( t ) )
            {
                // we need to push the current task and add our event to a new task
                if( !TraceAtlas::Markov::Q.push(TraceAtlas::Markov::taskBuffer.at(std::this_thread::get_id()), true) )
                {
    #ifdef DEBUG
                    printf("Task queue push returned error code\n");
    #endif
                }
                TraceAtlas::Markov::taskBuffer.at(std::this_thread::get_id()).reset();
                TraceAtlas::Markov::taskBuffer.at(std::this_thread::get_id()).addEvent( t );
            }
        }

        // caller hash table
        if (funcEntrance)
        {
            TraceAtlas::Markov::callInc.at(std::this_thread::get_id()).position = 0;
            if( TraceAtlas::Markov::threadSpawns.find(a) != TraceAtlas::Markov::threadSpawns.end() )
            {
                // the src of this caller edge is the last launcher
                TraceAtlas::Markov::callInc.at(std::this_thread::get_id()).src = TraceAtlas::Markov::lastLauncher;
            }
            else
            {
                // the src of this caller edge is the edge src node
                TraceAtlas::Markov::callInc.at(std::this_thread::get_id()).src = TraceAtlas::Markov::edgeInc.at(std::this_thread::get_id()).src;
            }
            TraceAtlas::Markov::callInc.at(std::this_thread::get_id()).snk = a;
            
            auto t = TraceAtlas::Markov::TB.getPtr( TraceAtlas::Markov::callInc.at(std::this_thread::get_id()) );
            if( !TraceAtlas::Markov::taskBuffer.at(std::this_thread::get_id()).addEvent( t ) )
            {
                // we need to push the current task and add our event to a new task
                if( !TraceAtlas::Markov::Q.push(TraceAtlas::Markov::taskBuffer.at(std::this_thread::get_id()), true) )
                {
    #ifdef DEBUG
                    printf("Task queue push returned error code\n");
    #endif
                }
                TraceAtlas::Markov::taskBuffer.at(std::this_thread::get_id()).reset();
                TraceAtlas::Markov::taskBuffer.at(std::this_thread::get_id()).addEvent( t );
            }
        }
        TraceAtlas::Markov::miners--;
    }
    void MarkovLaunch(uint64_t a)
    {
        // stores the block that is about to launch a thread
        TraceAtlas::Markov::launchers.insert(a);
        TraceAtlas::Markov::lastLauncher = a;
    }
    void TraceAtlasMarkovKernelEnter(char *label)
    {
        TraceAtlas::Markov::pushLabelStack(label);
    }
    void TraceAtlasMarkovKernelExit()
    {
        TraceAtlas::Markov::popLabelStack();
    }
}
