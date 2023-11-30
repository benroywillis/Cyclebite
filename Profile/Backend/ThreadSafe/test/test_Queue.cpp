//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//

#include "ThreadSafeQueue.h"
#include <thread>
#include <iostream>
#include <omp.h>
#include <map>

#define EVENTS  4000
#define WRITERS (EVENTS / TASK_SIZE)
#define READERS 2

using namespace std;
using namespace Cyclebite::Profile::Backend;

// map used to confirm all tasks are written exactly once into the queue
map<uint64_t, uint32_t> pushedMap;
// map used to confirm all tasks are read exactly once out of the queue
map<uint64_t, uint32_t> poppedMap;
// signifies that the writers are done pushing to the queue
bool writersDone = 0;

void writer(ThreadSafeQueue* Q, const Task& t)
{
    auto pushed = Q->push(t, true);
    if( pushed )
    {
        cout << "This is a writer. Just wrote task " << t.ID() << " to the queue and the size of the queue is now " << Q->members() << endl;
        try
        {
            pushedMap.at(t.ID())++;
        }
        catch( exception& e )
        {
            cout << "Just caught exception: " << e.what() << endl;
            cout << "Just pushed a bad task of ID " << t.ID() << "! Quitting..." << endl;
            exit(EXIT_FAILURE);
        }
    }
    else
    {
        cout << "This is a writer. Failed to write task " << t.ID() << " to the queue of size " << Q->members() << endl;
        cout << "Trying again..." << endl;
        writer(Q, t);
    }
}

void reader(ThreadSafeQueue* Q)
{
    while( !writersDone || Q->members() )
    {
        auto t = Q->pop(true);
        if( t.ID() == __LONG_MAX__ )
        {
            cout << "Just got a bad task from a pop with the queue at size " << Q->members() << "!" << endl;
            continue;
        }
        cout << "This is a reader. Just read task " << t.ID() << " and the size of the queue is now " << Q->members() << endl;
        try
        {
            poppedMap.at(t.ID())++;
        }
        catch( exception& e )
        {
            cout << "Just caught exception: " << e.what() << endl;
            cout << "Just read a bad task of ID " << t.ID() << "! Quitting..." << endl;
            exit(EXIT_FAILURE);
        }
    }
}

int main()
{
    // thread-safe queue that will be put under test
    ThreadSafeQueue Q;
    writersDone = false;
    // spawn many threads and make them all push to and read from the queue
    // Test 1: see if the pushing and popping works as expected
    //  a. push to the queue with multiple threads
    //  b. pop from the queue while the pushing is happening
    //  c. we should see expected behavior
    
    // fake events
    shared_ptr<EdgeInc> edges[EVENTS];
    for( unsigned i = 0; i < EVENTS; i++ )
    {
        edges[i] = make_shared<EdgeInc>();
        edges[i]->src = i;
        edges[i]->snk = i == (EVENTS-1) ? 0 : i + 1;
    }

    // bunch events into tasks
    Task tasks[EVENTS / TASK_SIZE];
    for( unsigned i = 0; i < EVENTS / TASK_SIZE; i++ )
    {
        pushedMap[tasks[i].ID()] = 0;
        poppedMap[tasks[i].ID()] = 0;
        for( unsigned j = 0; j < TASK_SIZE; j++ )
        {
            tasks[i].stuff[j] = edges[i*TASK_SIZE + j];
        }
    }

    // thread launches
    std::thread* writers[WRITERS];
    std::thread* readers[READERS];
    #pragma omp parallel sections
    {
        // turning on the parallel for's makes the behavior even more unpredicable
        #pragma omp section
        {
            // writers
            #pragma omp parallel for
            for( unsigned i = 0; i < WRITERS; i++ )
            {
                auto w = new std::thread(writer, &Q, tasks[i]);
                writers[i] = w;
            }
        }
        #pragma omp section
        {
            // readers
            //#pragma omp parallel for
            for( unsigned i = 0; i < READERS; i++ )
            {
                auto r = new std::thread(reader, &Q);
                readers[i] = r;
            }
        }
    }

    // thread join
    for( unsigned i = 0; i < WRITERS; i++ )
    {
        writers[i]->join();
    }
    writersDone = true;
    for( unsigned i = 0; i < READERS; i++ )
    {
        readers[i]->join();
    }

    // results
    uint64_t totalWrite = 0;
    for( const auto& entry : pushedMap )
    {
        totalWrite += entry.second;
        if( entry.second != 1 )
        {
            cout << "Task " << entry.first << " was written to the queue " << entry.second << " times." << endl;
        }
    }
    uint64_t totalRead = 0;
    for( const auto& entry : poppedMap )
    {
        totalRead += entry.second;
        if( entry.second != 1 )
        {
            cout << "Task " << entry.first << " was read from the queue " << entry.second << " times." << endl;
        }
    }
    cout << totalWrite << " total writes took place during the test." << endl;
    cout << totalRead  << " total reads took place during the test." << endl;
    cout << "The ending queue has " << Q.members() << " active entries in it." << endl;

    // free our thread objects
    for( unsigned i = 0; i < WRITERS; i++ )
    {
        delete writers[i];
    }
    for( unsigned i = 0; i < READERS; i++ )
    {
        delete readers[i];
    }
    return 0;
}