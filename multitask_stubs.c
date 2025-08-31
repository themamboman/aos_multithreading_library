/*
 * Multitask Library for Amiga OS3.x
 * Library stubs for linking
 * 
 * NOT USED in static library build - kept for reference only
 * 
 * This file provides stubs for linking against a shared library (.library)
 * but since we're building as a static library (.a), these stubs are not needed.
 * 
 * To build as a shared library, you would need:
 * 1. Full LVO table in multitask_lib.c
 * 2. Proper library entry/exit points
 * 3. These stubs for linking
 */

#include "multitask_lib.h"

/* Library function stubs */
struct MyThread *MyThreadCreate(ThreadFunc func, void *arg)
{
    return (struct MyThread *)CallLibFunction((struct Library *)MultitaskBase, -30, func, arg);
}

void MyThreadDestroy(struct MyThread *thread)
{
    CallLibFunction((struct Library *)MultitaskBase, -36, thread);
}

BOOL MyThreadIsRunning(struct MyThread *thread)
{
    return (BOOL)CallLibFunction((struct Library *)MultitaskBase, -42, thread);
}

void MyThreadWait(struct MyThread *thread)
{
    CallLibFunction((struct Library *)MultitaskBase, -48, thread);
}

void MyMutexInit(struct MyMutex *mutex)
{
    CallLibFunction((struct Library *)MultitaskBase, -54, mutex);
}

void MyMutexLock(struct MyMutex *mutex)
{
    CallLibFunction((struct Library *)MultitaskBase, -60, mutex);
}

void MyMutexUnlock(struct MyMutex *mutex)
{
    CallLibFunction((struct Library *)MultitaskBase, -66, mutex);
}

void MyMutexDestroy(struct MyMutex *mutex)
{
    CallLibFunction((struct Library *)MultitaskBase, -72, mutex);
}

struct MyMessagePort *MyPortCreate(void)
{
    return (struct MyMessagePort *)CallLibFunction((struct Library *)MultitaskBase, -78);
}

void MyPortDestroy(struct MyMessagePort *my_port)
{
    CallLibFunction((struct Library *)MultitaskBase, -84, my_port);
}

struct Message *MyPortWaitMessage(struct MyMessagePort *my_port)
{
    return (struct Message *)CallLibFunction((struct Library *)MultitaskBase, -90, my_port);
}

void MyPortPostMessage(struct MyMessagePort *my_port, struct Message *msg)
{
    CallLibFunction((struct Library *)MultitaskBase, -96, my_port, msg);
}

/* Condition variable stubs */
void MyConditionInit(struct MyCondition *cond)
{
    CallLibFunction((struct Library *)MultitaskBase, -102, cond);
}

void MyConditionWait(struct MyMutex *mutex, struct MyCondition *cond)
{
    CallLibFunction((struct Library *)MultitaskBase, -108, mutex, cond);
}

void MyConditionSignal(struct MyCondition *cond)
{
    CallLibFunction((struct Library *)MultitaskBase, -114, cond);
}

void MyConditionBroadcast(struct MyCondition *cond)
{
    CallLibFunction((struct Library *)MultitaskBase, -120, cond);
}

void MyConditionDestroy(struct MyCondition *cond)
{
    CallLibFunction((struct Library *)MultitaskBase, -126, cond);
}

/* TLS stubs */
MyTLSKey MyTLSCreate(void)
{
    return (MyTLSKey)CallLibFunction((struct Library *)MultitaskBase, -132);
}

void *MyTLSGet(MyTLSKey key)
{
    return (void *)CallLibFunction((struct Library *)MultitaskBase, -138, key);
}

void MyTLSSet(MyTLSKey key, void *value)
{
    CallLibFunction((struct Library *)MultitaskBase, -144, key, value);
}

void MyTLSDestroy(MyTLSKey key)
{
    CallLibFunction((struct Library *)MultitaskBase, -150, key);
}

/* Atomic stubs */
void MyAtomicInit(struct MyAtomic *atomic, LONG initial_value)
{
    CallLibFunction((struct Library *)MultitaskBase, -156, atomic, initial_value);
}

LONG MyAtomicLoad(struct MyAtomic *atomic)
{
    return (LONG)CallLibFunction((struct Library *)MultitaskBase, -162, atomic);
}

void MyAtomicStore(struct MyAtomic *atomic, LONG value)
{
    CallLibFunction((struct Library *)MultitaskBase, -168, atomic, value);
}

void MyAtomicAdd(struct MyAtomic *atomic, LONG delta)
{
    CallLibFunction((struct Library *)MultitaskBase, -174, atomic, delta);
}

LONG MyAtomicCompareExchange(struct MyAtomic *atomic, LONG expected, LONG desired)
{
    return (LONG)CallLibFunction((struct Library *)MultitaskBase, -180, atomic, expected, desired);
}

/* Run loop stubs */
void MyRunLoopInit(struct MyRunLoop *loop)
{
    CallLibFunction((struct Library *)MultitaskBase, -186, loop);
}

void MyRunLoopRun(struct MyRunLoop *loop)
{
    CallLibFunction((struct Library *)MultitaskBase, -192, loop);
}

void MyRunLoopStop(struct MyRunLoop *loop)
{
    CallLibFunction((struct Library *)MultitaskBase, -198, loop);
}

void MyRunLoopPost(struct MyRunLoop *loop, MyRunLoopFunc func, void *data)
{
    CallLibFunction((struct Library *)MultitaskBase, -204, loop, func, data);
}

struct MyTimer *MyRunLoopAddTimer(struct MyRunLoop *loop, ULONG interval_ms, MyRunLoopFunc cb, void *data, BOOL repeat)
{
    return (struct MyTimer *)CallLibFunction((struct Library *)MultitaskBase, -210, loop, interval_ms, cb, data, repeat);
}

void MyRunLoopRemoveTimer(struct MyRunLoop *loop, struct MyTimer *timer)
{
    CallLibFunction((struct Library *)MultitaskBase, -216, loop, timer);
}

void MyRunLoopDestroy(struct MyRunLoop *loop)
{
    CallLibFunction((struct Library *)MultitaskBase, -222, loop);
}

/* Library open/close functions */
struct MultitaskBase *MultitaskOpen(void)
{
    return (struct MultitaskBase *)OpenLibrary("multitask.library", MULTITASK_LIB_VERSION);
}

void MultitaskClose(struct MultitaskBase *base)
{
    if (base) {
        CloseLibrary((struct Library *)base);
    }
}
