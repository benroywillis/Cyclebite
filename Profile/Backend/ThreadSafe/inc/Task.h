#pragma once
#include <cstdint>
#include <vector>
//#include <llvm/Support/Casting.h>

typedef struct HashTable __TA_HashTable;

namespace TraceAtlas::Profile::Backend
{
    // most general event that can take place during a dynamic profile
    class Event
    {
    public:
        // Discriminator for LLVM-style RTTI
        enum class EventKind {
            Mem,
            Edge,
            Call,
            Label
        };
        // current block, this is the current state of the program (the basic block the program is currently executing)
        uint64_t snk;
        Event(EventKind k)
        {
            K = k;
        }
        virtual ~Event() = default;
        EventKind getKind() const
        {
            return K;
        }
    private:
        EventKind K;
    };

    // memory information from a dynamic profile
    class MemInc : public Event
    {
    public:
        MemInc() : Event(Event::EventKind::Mem) {}
        ~MemInc() = default;
        // base pointer of the memory transaction
        uint64_t addr;
        // offset of the memory transaction
        uint64_t offset;
    };

    // edge traversal information from a dynamic profile
    class EdgeInc : public Event
    {
    public:
        EdgeInc() : Event(Event::EventKind::Edge) {}
        ~EdgeInc() = default;
        // source block ID
        uint64_t src;
    };

    // callgraph information from a dynamic profile
    class CallInc : public Event
    {
    public:
        CallInc() : Event(Event::EventKind::Call) {}
        ~CallInc() = default;
        // caller block ID
        uint64_t src;
        // position of the caller within the calling basic block
        uint64_t position;
    };

    class LabelEvent : public Event
    {
    public:
        LabelEvent() : Event(Event::EventKind::Label) {}
        ~LabelEvent() = default;
        // label string
        char* label;
    };

    // number of things to do in a task that holds the queue
    constexpr int TASK_SIZE = 6;

    class Task
    {
    public:
        // things to do, has length TASK_SIZE
        Task(bool valid=true);
        Task(const std::vector<Event*>& events);
        ~Task() = default;
        uint64_t ID() const;
        int tasks() const;
        bool addEvent(Event* event);
        int pushTasks(__TA_HashTable* edge, __TA_HashTable* call, __TA_HashTable* label) const;
        void reset();
    private:
        uint64_t id;
        int taskCount;
        Event* stuff[TASK_SIZE];
        static uint64_t nextID;
        static uint64_t getNextID()
        {
            return nextID++;
        }
    };
} // namespace TraceAtlas::Profile::Backend