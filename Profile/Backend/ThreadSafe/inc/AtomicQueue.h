//==------------------------------==//
// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
//==------------------------------==//
#include "Task.h"
#include <iostream>
#include <atomic>
#include <mutex>

#define TRAINING_WHEELS     false

namespace Cyclebite::Profile::Backend
{
    // size of the circular buffer 
    constexpr uint32_t QUEUE_SIZE = 256;
    // number of tries before a blocking operation (push or pop) will give up
    constexpr uint32_t TRIES_MAX  = 32768;
    // number of microseconds a trying reader/writer will sleep between tries
    constexpr uint64_t SLEEP_TIME = 10;
    // TODO
    // 1. The reader method needs to check if r is a valid pointer
    // 2. The current behavior has non-deterministic behavior when it comes to the ending size of the queue
    class AtomicQueue
    {
    public:
        typedef uint8_t p_offset_t;
        AtomicQueue();
        ~AtomicQueue();
        bool push(const Task &newTask, bool block = false);
        Task pop(bool block = false);
        bool empty(const p_offset_t r, const p_offset_t w, const uint32_t e) const;
        bool full(const p_offset_t r, const p_offset_t w, const uint32_t e) const;
        uint64_t members() const;
    private:
        std::atomic<uint32_t> entries;
        std::atomic<p_offset_t> p_read;
        std::atomic<p_offset_t> p_write;
        mutable std::mutex QL_writer;
        mutable std::mutex QL_reader;
        // queue, implemented as a circular buffer
        Task *array;
    };
} // namespace Cyclebite::Profile::Backend

// John: you should only have to lock in the wrap-around case (when the write pointer goes from the end of the queue array to the beginning)
// John: danger in the queue -> when two readers do a read at the same time, they collide in the pointer increment, and thus they read the same place, which is a bug (fix: atomic increment)
//       - same exists in the write-write.. but as long as the places to write are different, we are good
// Ben: case when two writers clash, the "first" writer makes the queue full, and the second writer reads the old size value
//      - john: this is one version of the write-read conflict
//        -- fix: lock on "almost full" ie one from full
//        -- idea: keep an "initial" read pointer (which is read by the readers) and "final" read pointer (which is read by the writers) (John: total store ordering guarantees that the reading thread reads after the writer thread)
//           --- this will not help the case when the queue is middle-occupency (because it slows all our transactions down with two atomics instead of one)
//        -- I keep an occupency pointer that is the last thing a transaction touches, and this will tame the behavior between readers and writers
//      - "full" or "empty" - when we are full, or empty, the read pointer is the same as the write pointer
//      - "almost" full - the write pointer is one behind the read pointer (or at the tail and the read is at the head)
//      - "almost" empty - the write pointer is one ahead of the read pointer
//      - in both of the above cases you want to lock the read and write
//      - you also want to lock when you are wrapping around the circular buffer
// John: the atomic increments will make the hash table thread-safe
//       - and thus we only need atomic increments for hash table increments
//       - we need to lock the hash entry when we need to change size
//
// John: corner cases that result in the read, write pointers being close together
//  - when the queue is near-empty
//  - when the queue is near-full
//  - when the queue is empty -> then we don't do anything
//  - when the queue is full -> we need to stall the user-space program and let the queue drain a little bit

// Ben: conflicts
// 1. write-read:
//    - causes:
//       a. two writers clash, both read the same write pointer and the second writes over the first
//       b. two writers clash, both read the same write pointer, the first writer makes the queue full
//       c. two readers clash, both read the same read pointer, both do the same task and only pop one entry
//       d. two readers clash, both read the same read pointer, the first reader makes the queue empty
//       e. reader and writer clash, the reader makes the queue empty, the writer
// 2. read-write:
// 3. read-read: when a reader and another reader pop the queue at the same time, or when two readers push to the queue at the same time
//    - problems
//       a. two (or more) readers pop a queue of size 1 multiple times - one reader gets an invalid pointer
//       b. two readers get the same read pointer and update the same edge frequency twice
//          -- made less likely by the auto r = atomic++ code (ie we want to read and increment at the same time)
//          -- in this case we need to be able to detect that these edges have already been touched.. we can do this by giving entries in the queue a unique ID, and when the queue detects the same entry twice it rejects the second one
// 4. write-write: one writer writes over the other