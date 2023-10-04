// Copyright 2023 Benjamin Willis
// SPDX-License-Identifier: Apache-2.0
#include "ThreadSafeQueue.h"

using namespace std;

namespace Cyclebite::Profile::Backend
{
    // size method is private to prevent races between it and pop()
    bool ThreadSafeQueue::empty() const
    {
        return _queue.members() == 0;
    }

    ThreadSafeQueue::~ThreadSafeQueue() {}

    unsigned long ThreadSafeQueue::members() const
    {
        return _queue.members();
    }

    Task ThreadSafeQueue::pop(bool block)
    {
        return _queue.pop(block);
    }

    bool ThreadSafeQueue::push(const Task& newTask, bool block )
    {
        return _queue.push(newTask, block);
    }
} // namespace Cyclebite::Profile::Backend