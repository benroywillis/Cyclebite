#include "Task.h"
#include "DashHashTable.h"
#include <iostream>

using namespace std;
using namespace TraceAtlas::Profile::Backend;

Task::Task(bool valid)
{
    if( valid )
    {
        id = getNextID();
    }
    else
    {
        id = __LONG_MAX__;
    }
    taskCount = 0;
}

Task::Task(const vector<Event*>& events)
{
    if( events.empty() )
    {
        id = __LONG_MAX__;
        taskCount = 0;
        return;
    }
    id = getNextID();
    taskCount = (int)events.size();
    for( unsigned i = 0; i < events.size(); i++ )
    {
        stuff[i] = events[i];
    }
}

uint64_t Task::ID() const
{
    return id;
}

int Task::tasks() const
{
    return taskCount;
}

void Task::reset()
{
    id = getNextID();
    taskCount = 0;
}

bool Task::addEvent(Event* event)
{
    if( taskCount < TASK_SIZE )
    {
        stuff[taskCount++] = event;
        return true;
    }
    else
    {
        // if there is no more room we don't add the event and return a failure
        return false;
    }
}

// helpers
int pushEdgeInc( __TA_HashTable* t, const EdgeInc* event )
{
    __TA_edgeTuple edge;
    edge.blocks[0] = (uint32_t)event->src;
    edge.blocks[1] = (uint32_t)event->snk;
    edge.frequency = 0;
    __TA_element e;
    e.edge = edge;
    while (__TA_HashTable_increment(t, &e))
    {
#ifdef DEBUG
        cout << "Resolving clash in edge table" << endl;
#endif
        if( __TA_resolveClash(t, t->size + 1) )
        {
            // somebody else has already started building a new mine
            // Wait until they are done before looping around again
            while( t->newMine ) 
            {
#ifdef DEBUG
                printf("Waiting on new mine...\n");
#endif
            }
        }
    }
    return 0;
}

int pushCallInc( __TA_HashTable* t, const CallInc* call )
{
    __TA_callerTuple caller;
    caller.blocks[0] = (uint32_t)call->src;
    caller.blocks[1] = (uint32_t)call->snk;
    caller.position = 0;
    __TA_element e;
    e.callee = caller;
    while (__TA_HashTable_increment(t, &e))
    {
#ifdef DEBUG
        cout << "Resolving clash in caller table" << endl;
#endif
        if( __TA_resolveClash(t, t->size + 1) )
        {
            // somebody else has already started building a new mine
            // Wait until they are done before looping around again
            while( t->newMine ) 
            {
#ifdef DEBUG
                printf("Waiting on new mine...\n");
#endif
            }
        }
    }
    return 0;
}

int pushLabelEvent( __TA_HashTable* t, const LabelEvent* label )
{
    __TA_labelTuple labeler;
    labeler.blocks[0] = (uint32_t)label->snk;
    // helps hash function randomization
    labeler.blocks[1] = 0;
    labeler.label = label->label;
    labeler.frequency = 1;
    __TA_element e;
    e.label = labeler;
    while (__TA_HashTable_increment_label(t, &e))
    {
#ifdef DEBUG
        cout << "Resolving clash in label table" << endl;
#endif
        if( __TA_resolveClash(t, t->size + 1) )
        {
            // somebody else has already started building a new mine
            // Wait until they are done before looping around again
            while( t->newMine ) 
            {
#ifdef DEBUG
                printf("Waiting on new mine...\n");
#endif
            }
        }
    }
    return 0;
}

int Task::pushTasks(__TA_HashTable* edge, __TA_HashTable* call, __TA_HashTable* label) const
{
    for( int i = 0; i < taskCount; i++ )
    {
        switch (stuff[i]->getKind()) 
        {
            case Event::EventKind::Mem:
                // can't handle this for now
                printf("Cannot yet handle memory increments!\n");
                break;
            case Event::EventKind::Edge:
                if( pushEdgeInc(edge, static_cast<EdgeInc*>(stuff[i])) )
                {
                    printf("Failed to push edge event %lu,%lu\n", static_cast<EdgeInc*>(stuff[i])->src, static_cast<EdgeInc*>(stuff[i])->snk);
                }
                break;            
            case Event::EventKind::Call:
                if( pushCallInc(call, static_cast<CallInc*>(stuff[i])) )
                {
                    printf("Failed to push call event %lu,%lu\n", static_cast<CallInc*>(stuff[i])->src, static_cast<CallInc*>(stuff[i])->snk);
                }
                break;
            case Event::EventKind::Label:
                if( pushLabelEvent(label, static_cast<LabelEvent*>(stuff[i])) )
                {
                    printf("Failed to push label event %lu,%s\n", static_cast<LabelEvent*>(stuff[i])->snk, static_cast<LabelEvent*>(stuff[i])->label);
                }
                break;
            default:
                printf("Could not recognize the type of event in this task!\n");
        }
    }
    return 0;
}