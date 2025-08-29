/*
 * Multitask Library for Amiga OS3.x
 * Main source file
 */

#include "multitask_lib.h"
#include <proto/exec.h>
#include <proto/dos.h>

/* Library function vectors */
static ULONG LibOpen(struct MultitaskBase *base);
static void LibClose(struct MultitaskBase *base);
static void LibExpunge(struct MultitaskBase *base);

/* Function vector table */
static const APTR LibVectors[] = {
    (APTR)LibOpen,
    (APTR)LibClose,
    (APTR)LibExpunge,
    (APTR)-1  /* End marker */
};

/* Library information */
static const char LibName[] = "multitask.library";
static const char LibID[] = "multitask.library 1.0 (1 Jan 2024)";

/* Library base pointer */
static struct MultitaskBase *LibBase = NULL;

/* Library initialization */
static struct Library *LibInit(struct MultitaskBase *base, BPTR seglist, struct ExecBase *exec)
{
    base->lib.lib_Node.ln_Type = NT_LIBRARY;
    base->lib.lib_Node.ln_Pri = 0;
    base->lib.lib_Node.ln_Name = (char *)LibName;
    base->lib.lib_Flags = LIBF_SUMUSED | LIBF_CHANGED;
    base->lib.lib_Version = MULTITASK_LIB_VERSION;
    base->lib.lib_Revision = MULTITASK_LIB_REVISION;
    base->lib.lib_IdString = (char *)LibID;
    
    /* Initialize library-specific data */
    base->version = MULTITASK_LIB_VERSION;
    base->revision = MULTITASK_LIB_REVISION;
    
    return (struct Library *)base;
}

/* Library open */
static ULONG LibOpen(struct MultitaskBase *base)
{
    base->lib.lib_OpenCnt++;
    base->lib.lib_Flags &= ~LIBF_DELEXP;
    return (ULONG)base;
}

/* Library close */
static void LibClose(struct MultitaskBase *base)
{
    base->lib.lib_OpenCnt--;
}

/* Library expunge */
static void LibExpunge(struct MultitaskBase *base)
{
    if (base->lib.lib_OpenCnt == 0) {
        base->lib.lib_Node.ln_Type = NT_UNKNOWN;
        DeleteLibrary((struct Library *)base);
        return;
    }
    base->lib.lib_Flags |= LIBF_DELEXP;
}

/* Thread entry point */
static LONG MyThreadEntry(void)
{
    struct MyThread *thread_data = (struct MyThread *)FindTask(NULL)->tc_UserData;
    ThreadFunc func = (ThreadFunc)thread_data->user_data;
    
    if (func) {
        func(thread_data->user_data);
    }
    
    thread_data->is_running = FALSE;
    return 0;
}

/* Create a new thread */
struct MyThread *MyThreadCreate(ThreadFunc func, void *arg)
{
    struct MyThread *thread = (struct MyThread *)AllocMem(sizeof(struct MyThread), MEMF_CLEAR);
    if (!thread) return NULL;

    thread->user_data = arg;
    thread->is_running = TRUE;

    /* Create the AmigaOS task */
    thread->os_task = CreateTask(
        "MyThread",           /* Task name */
        0,                    /* Priority */
        (APTR)MyThreadEntry, /* Entry function */
        2000                  /* Stack size */
    );

    if (!thread->os_task) {
        FreeMem(thread, sizeof(struct MyThread));
        return NULL;
    }

    /* Set the user data so the task can retrieve its state */
    thread->os_task->tc_UserData = (APTR)thread;

    return thread;
}

/* Destroy a thread */
void MyThreadDestroy(struct MyThread *thread)
{
    if (thread) {
        if (thread->os_task) {
            /* Signal the task to terminate */
            Signal(thread->os_task, SIGBREAKF_CTRL_C);
            /* Wait a bit for it to finish */
            Delay(1);
            /* Remove the task */
            RemTask(thread->os_task);
        }
        FreeMem(thread, sizeof(struct MyThread));
    }
}

/* Check if thread is running */
BOOL MyThreadIsRunning(struct MyThread *thread)
{
    if (thread && thread->os_task) {
        return (thread->os_task->tc_State != TS_DEAD);
    }
    return FALSE;
}

/* Wait for thread to complete */
void MyThreadWait(struct MyThread *thread)
{
    if (thread && thread->os_task) {
        while (thread->os_task->tc_State != TS_DEAD) {
            Delay(1);
        }
    }
}

/* Initialize a mutex */
void MyMutexInit(struct MyMutex *mutex)
{
    if (mutex) {
        InitSemaphore(&mutex->sem);
    }
}

/* Lock a mutex */
void MyMutexLock(struct MyMutex *mutex)
{
    if (mutex) {
        ObtainSemaphore(&mutex->sem);
    }
}

/* Unlock a mutex */
void MyMutexUnlock(struct MyMutex *mutex)
{
    if (mutex) {
        ReleaseSemaphore(&mutex->sem);
    }
}

/* Destroy a mutex */
void MyMutexDestroy(struct MyMutex *mutex)
{
    /* SignalSemaphore doesn't need explicit cleanup in AmigaOS */
}

/* Create a message port */
struct MyMessagePort *MyPortCreate(void)
{
    struct MyMessagePort *my_port = (struct MyMessagePort *)AllocMem(sizeof(struct MyMessagePort), MEMF_CLEAR);
    if (!my_port) return NULL;

    my_port->signal = AllocSignal(-1);
    if (my_port->signal == -1) {
        FreeMem(my_port, sizeof(struct MyMessagePort));
        return NULL;
    }

    my_port->port.mp_Node.ln_Type = NT_MSGPORT;
    my_port->port.mp_Flags = PA_SIGNAL;
    my_port->port.mp_SigBit = my_port->signal;
    my_port->port.mp_SigTask = FindTask(NULL);
    NewList(&my_port->port.mp_MsgList);

    return my_port;
}

/* Destroy a message port */
void MyPortDestroy(struct MyMessagePort *my_port)
{
    if (my_port) {
        if (my_port->signal != -1) {
            FreeSignal(my_port->signal);
        }
        FreeMem(my_port, sizeof(struct MyMessagePort));
    }
}

/* Wait for a message */
struct Message *MyPortWaitMessage(struct MyMessagePort *my_port)
{
    struct Message *msg;
    if (!my_port) return NULL;
    
    Wait(1L << my_port->signal);
    while ((msg = (struct Message *)GetMsg(&my_port->port))) {
        return msg;
    }
    return NULL;
}

/* Post a message to a port */
BOOL MyPortPostMessage(struct MyMessagePort *my_port, struct Message *msg)
{
    if (my_port && msg) {
        return PutMsg(&my_port->port, msg);
    }
    return FALSE;
}

/* Library open function for external use */
struct MultitaskBase *MultitaskOpen(void)
{
    return (struct MultitaskBase *)OpenLibrary(LibName, MULTITASK_LIB_VERSION);
}

/* Library close function for external use */
void MultitaskClose(struct MultitaskBase *base)
{
    if (base) {
        CloseLibrary((struct Library *)base);
    }
}

/* Library entry point */
struct Library *LibEntry(void)
{
    struct MultitaskBase *base = (struct MultitaskBase *)AllocMem(sizeof(struct MultitaskBase), MEMF_CLEAR);
    if (!base) return NULL;
    
    return LibInit(base, 0, SysBase);
}

/* Library exit point */
void LibExit(void)
{
    /* Cleanup if needed */
}
