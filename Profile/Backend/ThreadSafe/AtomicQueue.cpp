//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "AtomicQueue.h"
#include <unistd.h>

using namespace Cyclebite::Profile::Backend;
using namespace std;

uint64_t Task::nextID = 0;

AtomicQueue::AtomicQueue()
{
    array = (Task *)calloc(QUEUE_SIZE, sizeof(Task));
    for( unsigned i = 0; i < QUEUE_SIZE; i++ )
    {
        array[i] = Task(false);
    }
    p_read  = 0;
    p_write = 0;
    entries = 0;
}

AtomicQueue::~AtomicQueue()
{
    free(array);
}

bool AtomicQueue::push(const Task &newTask, bool block)
{
    QL_writer.lock();
    volatile p_offset_t w = p_write;
    volatile p_offset_t r = p_read;
    volatile uint32_t e = entries;
    if( block )
    {
        uint32_t tries = 0;
        while( full(r, w, e) )
        {
            if( ++tries > TRIES_MAX )
            {
                QL_writer.unlock();
                return false;
            }
            usleep(SLEEP_TIME);
            r = p_read;
            e = entries;
        }
        p_write++;
        entries++;
        array[w] = newTask;
        QL_writer.unlock();
        return true;
    }
    else
    {
        if( full(r, w, e) )
        {
            QL_writer.unlock();
            return false;
        }
        else
        {
            p_write++;
            entries++;
            array[w] = newTask;
            QL_writer.unlock();
            return true;
        }
    }
}

Task AtomicQueue::pop(bool block)
{
    QL_reader.lock();
    volatile p_offset_t r = p_read;
    volatile p_offset_t w = p_write;
    volatile uint32_t e = entries;
    if( block )
    {
        uint32_t tries = 0;
        while( empty(r, w, e) )
        {
            if( ++tries > TRIES_MAX )
            {
#if TRAINING_WHEELS
                cout << "Exceeded the maximum numbers of tries... Quitting" << endl;
#endif
                QL_reader.unlock();
                return Task(false);
            }
            usleep(SLEEP_TIME);
            w = p_write;
            e = entries;
#if TRAINING_WHEELS
            cout << "Read an invalid pop pointer! Blocking until this read location is valid..." << endl;
#endif
        }
    }
    else
    {
        if ( empty(r, w, e) )
        {
            // we do not have a valid read pointer, sound off and return nothing
#if TRAINING_WHEELS
            cout << "Read an invalid pop pointer! Returning nothing.." << endl;
#endif
            QL_reader.unlock();
            return Task(false);
        }
    }
    // here we do exactly the operation the final pointer that we did to the initial pointer
    // this is to account for the case when we have many readers that are being blocked, then race to the initial pointer, then the final pointer
    p_read++;
    entries--;
    QL_reader.unlock();
    return array[r];
}

bool AtomicQueue::empty(const p_offset_t r, const p_offset_t w, const uint32_t e) const
{
    // there are two cases
    // first: p_read is "behind" p_write, meaning neither pointer has wrapped around
    // second: p_read is "ahead" of p_write, meaning the write pointer has wrapped around and the reader has not
    // in either case, the following condition will work
    return (r == w) || (e <= 0);
}

bool AtomicQueue::full(const p_offset_t r, const p_offset_t w, const uint32_t e) const
{
    // the full condition is when the write pointer is about to "run over" the read pointer ie we are about to write over an entry that should be read first
    // general condition: read and write are somewhere in the middle of the buffer, then write will be one behind the read in order for the buffer to be full
    // boundary condition: read is at the beginning and write is at the end. Then write+1 will wrap around and equal read
    // thus the below condition satisfies both cases
    return ( r == w+1 ) || ( e >= QUEUE_SIZE-1 );
}

uint64_t AtomicQueue::members() const
{
    return entries;
}