/*
 * Multitask Library for Amiga OS3.x
 * Library stubs for linking
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

BOOL MyPortPostMessage(struct MyMessagePort *my_port, struct Message *msg)
{
    return (BOOL)CallLibFunction((struct Library *)MultitaskBase, -96, my_port, msg);
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
