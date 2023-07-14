
#include "ThreadSafeQueue.h"
#include "DashHashTable.h"
#include <thread>
#include <iostream>
#include <omp.h>
#include <map>
#include <math.h>

#define EVENTS  4000
#define TASKS   ((EVENTS / TASK_SIZE) + 1)
#define WRITERS 1
#define READERS 1

using namespace std;
using namespace Cyclebite::Profile::Backend;

// map used to confirm all tasks are written exactly once into the queue
map<uint64_t, uint32_t> pushedMap;
// map used to confirm all tasks are read exactly once out of the queue
map<uint64_t, uint32_t> poppedMap;
// signifies that the writers are done pushing to the queue
bool writersDone = false;

void writer(ThreadSafeQueue* Q, vector<Task> ts)
{
    for( const auto& t : ts )
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
            vector<Task> doTask;
            doTask.push_back(t);
            writer(Q, doTask);
        }
    }
}

void reader(ThreadSafeQueue* Q, HashTable* ht)
{
    while( !writersDone || Q->members() )
    {
        auto t = Q->pop(true);
        if( t.ID() == __LONG_MAX__ )
        {
            cout << "Just got a bad task from a pop with the queue at size " << Q->members() << "!" << endl;
            continue;
        }
        cout << "This is a reader. Pushing task " << t.ID() << " to the hash table." << endl;
		auto ret = t.pushTasks(ht);
        cout << "Pushed tasks to hash table with exit code " << ret << endl;
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
    cout << "Reader exiting..." << endl;
}

int main()
{
    // thread-safe queue that will be put under test
    ThreadSafeQueue Q;
    struct HashTable   HT;
	HT.size = (uint32_t)( ceil(log(100.0)) / log(2.0) );
	HT.getFullSize = __TA_getFullSize;
	HT.array = (__TA_arrayElem*)calloc(HT.getFullSize(&HT), sizeof(__TA_arrayElem));
    HT.miners = 0;
    HT.newMine = false;
    writersDone = false;
    // spawn many threads and make them all push to and read from the queue
    // Test 1: see if the pushing and popping works as expected
    //  a. push to the queue with multiple threads
    //  b. pop from the queue while the pushing is happening
    //  c. we should see expected behavior
    
    // fake events
    EdgeInc* edges[EVENTS];
    for( unsigned i = 0; i < EVENTS; i++ )
    {
        edges[i] = new EdgeInc();
        edges[i]->src = i;
        edges[i]->snk = i == (EVENTS-1) ? 0 : i + 1;
    }

    // bunch events into tasks
    Task* tasks = (Task*)malloc(TASKS*sizeof(Task));
    for( unsigned i = 0; i < TASKS; i++ )
    {
        vector<EdgeInc*> ve;
        for( unsigned j = 0; j < TASK_SIZE; j++ )
        {
            if( (i*TASK_SIZE + j) > EVENTS )
            {
                continue;
            }
            ve.push_back(edges[i*TASK_SIZE + j]);
        }

        //tasks[i] = Task(ve);
		for( unsigned j = 0; j < TASK_SIZE; j++ )
		{
			tasks[i].addEvent(ve[i]);
		}
        pushedMap[tasks[i].ID()] = 0;
        poppedMap[tasks[i].ID()] = 0;
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
                // each writer gets a fraction of the tasks to do
                vector<Task> taskList;
                for( unsigned j = i*TASKS/WRITERS; j < (i+1)*TASKS/WRITERS; j++ )
                {
                    if( j > TASKS )
                    {
                        continue;
                    }
                    taskList.push_back(tasks[j]);
                }
                auto w = new std::thread(writer, &Q, taskList);
                writers[i] = w;
            }
        }
        #pragma omp section
        {
            // readers
            #pragma omp parallel for
            for( unsigned i = 0; i < READERS; i++ )
            {
                auto r = new std::thread(reader, &Q, &HT);
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
	// the hash table check confirms that all edges are in there and have the correct frequency
	for( unsigned i = 0; i < EVENTS; i++ )
	{
		__TA_edgeTuple edge;
		edge.frequency = 1;
		edge.blocks[0] = i;
		if( i < EVENTS-1 )
		{
			edge.blocks[1] = i+1;
		}
		else
		{
			edge.blocks[1] = 0;
		}
		__TA_element search;
		search.edge = edge;
		auto e = __TA_HashTable_read(&HT, &search);
		if( !e ) 
		{
			cout << "Edge " << edge.blocks[0] << "," << edge.blocks[1] << " was not found in the hash table." << endl;
		}
		else if( e->edge.frequency != 1 )
		{
			cout << "Edge " << edge.blocks[0] << "," << edge.blocks[1] << " had a frequency of " << e->edge.frequency << " when it should've been 1." << endl;
		}
	}

    // free our thread objects and hash table
    for( unsigned i = 0; i < WRITERS; i++ )
    {
        delete writers[i];
    }
    for( unsigned i = 0; i < READERS; i++ )
    {
        delete readers[i];
    }
	free(HT.array);
	for( unsigned i = 0 ; i < EVENTS; i++ )
	{
		delete edges[i];
	}

    return 0;
}
