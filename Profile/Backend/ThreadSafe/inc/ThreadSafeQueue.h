#include "AtomicQueue.h"
#include <unistd.h>

namespace Cyclebite::Profile::Backend
{
    class ThreadSafeQueue
    {
    public:
        ThreadSafeQueue() = default;
        virtual ~ThreadSafeQueue();
        Task pop(bool block = false);
        bool push(const Task &newTask, bool block = false);
        uint64_t members() const;
    private:
        AtomicQueue _queue;
        // empty method is private to prevent races between it and pop
        bool empty() const;
        bool full() const;
    };
} // namespace Cyclebite::Profile::Backend