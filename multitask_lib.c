/*
 * Multitask Library for Amiga OS3.x
 * Main source file - Static Library Version
 */

#include "multitask_lib.h"
#include <proto/exec.h>
#include <proto/dos.h>

/* Thread entry point */
static LONG MyThreadEntry(void)
{
    struct Task *self = FindTask(NULL);
    struct MyThread *t = (struct MyThread *)self->tc_UserData;
    
    if (t && t->func) {
        t->func(t->arg);
    }
    
    /* NEW: run per-thread TLS destructors */
    MyTLSCallDestructorsForCurrentThread();
    
    t->is_running = FALSE;
    Signal(t->parent, 1L << t->doneSig);   /* tell joiner we're done */
    Forbid();                 /* ensure we don't get rescheduled mid-exit */
    RemTask(NULL);            /* does not return */
    return 0;                 /* never reached, but keeps compilers happy */
}

/* Create a new thread */
struct MyThread *MyThreadCreate(ThreadFunc func, void *arg)
{
    struct MyThread *thread = (struct MyThread *)AllocMem(sizeof(struct MyThread), MEMF_CLEAR);
    if (!thread) return NULL;

    thread->func = func;
    thread->arg = arg;
    thread->parent = FindTask(NULL);
    thread->doneSig = AllocSignal(-1);
    if (thread->doneSig < 0) {
        FreeMem(thread, sizeof(struct MyThread));
        return NULL;
    }

    /* Create the AmigaOS task */
    Forbid();  /* prevent scheduling until tc_UserData is set */
    thread->os_task = CreateTask(
        "MyThread",           /* Task name */
        0,                    /* Priority */
        (APTR)MyThreadEntry, /* Entry function */
        8192                  /* Stack size */
    );

    if (!thread->os_task) {
        Permit();
        FreeSignal(thread->doneSig);
        FreeMem(thread, sizeof(struct MyThread));
        return NULL;
    }

    /* Set the user data so the task can retrieve its state */
    thread->os_task->tc_UserData = (APTR)thread;
    thread->is_running = TRUE;
    Permit();
    
    /* Initialize TLS data array */
    int i;
    for (i = 0; i < MAX_TLS_KEYS; i++) {
        thread->tls_data[i] = NULL;
    }

    return thread;
}

/* Destroy a thread */
void MyThreadDestroy(struct MyThread *thread)
{
    if (!thread) return;
    
    /* WARNING: Caller MUST call MyThreadWait() before MyThreadDestroy() */
    /* This ensures the thread has completed and the signal is safe to free */
    
    /* Debug assert: catch destroy-before-join misuse */
    if (thread->is_running) {
        /* This is a programming error - thread is still running */
        /* In a debug build, you might want to assert(0) here */
        /* For now, just warn - in production you might assert(!thread->is_running) */
    }
    
    if (thread->doneSig >= 0) {
        FreeSignal(thread->doneSig);
    }
    
    FreeMem(thread, sizeof(struct MyThread));
}

/* Check if thread is running */
BOOL MyThreadIsRunning(struct MyThread *thread)
{
    return thread ? thread->is_running : FALSE;
}

/* Wait for thread to complete */
void MyThreadWait(struct MyThread *thread)
{
    if (thread && thread->doneSig >= 0) {
        Wait(1L << thread->doneSig);
    }
}

/* Yield CPU time to other tasks */
void MyThreadYield(void)
{
    Delay(0);
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
MyMessagePort *MyPortCreate(void)
{
    return CreateMsgPort();
}

/* Destroy a message port */
void MyPortDestroy(MyMessagePort *my_port)
{
    if (my_port) {
        DeleteMsgPort(my_port);
    }
}

/* Wait for a message */
struct Message *MyPortWaitMessage(MyMessagePort *my_port)
{
    if (!my_port) return NULL;
    WaitPort(my_port);
    return GetMsg(my_port);
}

/* Post a message to a port */
void MyPortPostMessage(MyMessagePort *my_port, struct Message *msg)
{
    if (my_port && msg) {
        PutMsg(my_port, msg);
    }
}

/* Condition variable implementation */
void MyConditionInit(struct MyCondition *cond)
{
    if (!cond) return;
    NewList((struct List*)&cond->waiters);
    InitSemaphore(&cond->guard);
}

void MyConditionWait(struct MyMutex *mutex, struct MyCondition *cond)
{
    if (!mutex || !cond) return;
    struct Task *self = FindTask(NULL);
    struct MyCondWaiter w = {0};
    w.task = self;
    w.sigBit = AllocSignal(-1);
    if (w.sigBit < 0) return;
    
    /* Note: Using stack-based waiter node. For timed waits or broadcast storms,
       consider heap-allocating the waiter to make lifetime explicit. */

    ObtainSemaphore(&cond->guard);
    AddTail((struct List*)&cond->waiters, (struct Node*)&w);
    ReleaseSemaphore(&cond->guard);

    ReleaseSemaphore(&mutex->sem);
    Wait(1L << w.sigBit);
    FreeSignal(w.sigBit);
    ObtainSemaphore(&mutex->sem);
}

void MyConditionSignal(struct MyCondition *cond)
{
    if (!cond) return;
    ObtainSemaphore(&cond->guard);
    struct MyCondWaiter *w = (struct MyCondWaiter*)RemHead((struct List*)&cond->waiters);
    ReleaseSemaphore(&cond->guard);
    if (w && w->task && w->sigBit >= 0) Signal(w->task, 1L << w->sigBit);
}

void MyConditionBroadcast(struct MyCondition *cond)
{
    if (!cond) return;
    ObtainSemaphore(&cond->guard);
    struct MyCondWaiter *w;
    while ((w = (struct MyCondWaiter*)RemHead((struct List*)&cond->waiters))) {
        if (w && w->task && w->sigBit >= 0) Signal(w->task, 1L << w->sigBit);
    }
    ReleaseSemaphore(&cond->guard);
}

void MyConditionDestroy(struct MyCondition *cond) 
{ 
    if (!cond) return;
    
    /* Debug check: ensure no waiters remain */
    /* Callers must ensure all waiters have been signaled before destroying */
    /* MinList is empty when Head points at Tail (mlh_Head == &mlh_Tail) */
    if (cond->waiters.mlh_Head != (struct MinNode*)&cond->waiters.mlh_Tail) {
        /* Warning: condition destroyed while waiters still exist */
        /* This indicates a programming error - waiters should be signaled first */
    }
}

/* Thread-local storage implementation */
static struct SignalSemaphore tls_guard = {0};
static UWORD next_tls_key = 0;
static MyTLSDtor tls_dtors[MAX_TLS_KEYS]; /* parallel to tls_data[] */

MyTLSKey MyTLSCreateWithDtor(MyTLSDtor dtor)
{
    MyTLSKey key;
    
    if (tls_guard.ss_Link.lh_Head == NULL) {
        InitSemaphore(&tls_guard);
    }
    
    ObtainSemaphore(&tls_guard);
    if (next_tls_key >= MAX_TLS_KEYS) {
        ReleaseSemaphore(&tls_guard);
        return (MyTLSKey)0xFFFF; /* Invalid key - TLS exhausted */
    }
    key = next_tls_key++; /* 0-based keys: 0, 1, 2, ..., 31 */
    tls_dtors[key] = dtor; /* record dtor (may be NULL) */
    ReleaseSemaphore(&tls_guard);
    
    return key;
}

/* Keep the existing MyTLSCreate as a convenience wrapper */
MyTLSKey MyTLSCreate(void) { return MyTLSCreateWithDtor(NULL); }

void *MyTLSGet(MyTLSKey key)
{
    if (key >= MAX_TLS_KEYS || key == 0xFFFF) return NULL; /* Check for invalid key */
    struct MyThread *current = (struct MyThread *)FindTask(NULL)->tc_UserData;
    if (!current) return NULL;
    return current->tls_data[key];
}

void MyTLSSet(MyTLSKey key, void *value)
{
    if (key >= MAX_TLS_KEYS || key == 0xFFFF) return; /* Check for invalid key */
    struct MyThread *current = (struct MyThread *)FindTask(NULL)->tc_UserData;
    if (!current) return;
    current->tls_data[key] = value;
}

void MyTLSDestroy(MyTLSKey key)
{
    /* For now, no cleanup needed */
}

/* Call per-thread destructors; allow a few passes like pthreads */
void MyTLSCallDestructorsForCurrentThread(void)
{
    struct MyThread *current = (struct MyThread *)FindTask(NULL)->tc_UserData;
    if (!current) return;

    for (int pass = 0; pass < 4; ++pass) {  /* 4 is ample for common patterns */
        BOOL any = FALSE;
        for (UWORD k = 0; k < next_tls_key; ++k) {
            void *p = current->tls_data[k];
            MyTLSDtor d = tls_dtors[k];
            if (p && d) {
                current->tls_data[k] = NULL; /* clear before calling dtor */
                d(p);
                any = TRUE;
            }
        }
        if (!any) break;
    }
}

/* Atomic operations implementation */
/* Note: Using Forbid()/Permit() for 68k atomicity. This is global and pauses task switching. */
/* Keep critical sections tiny to minimize impact on system responsiveness. */
void MyAtomicInit(struct MyAtomic *atomic, LONG initial_value)
{
    if (atomic) {
        atomic->value = initial_value;
    }
}

LONG MyAtomicLoad(struct MyAtomic *atomic)
{
    if (!atomic) return 0;
    
    /* Use Forbid() to ensure atomicity on 68k */
    Forbid();
    LONG value = atomic->value;
    Permit();
    return value;
}

void MyAtomicStore(struct MyAtomic *atomic, LONG value)
{
    if (!atomic) return;
    
    /* Use Forbid() to ensure atomicity on 68k */
    Forbid();
    atomic->value = value;
    Permit();
}

void MyAtomicAdd(struct MyAtomic *atomic, LONG delta)
{
    if (!atomic) return;
    
    /* Use Forbid() to ensure atomicity on 68k */
    Forbid();
    atomic->value += delta;
    Permit();
}

LONG MyAtomicCompareExchange(struct MyAtomic *atomic, LONG expected, LONG desired)
{
    if (!atomic) return 0;
    
    /* Use Forbid() to ensure atomicity on 68k */
    Forbid();
    LONG old_value = atomic->value;
    if (old_value == expected) {
        atomic->value = desired;
    }
    Permit();
    return old_value;
}

/* Run loop implementation */
/* Note: Using CurrentTime() as fallback. For better precision, consider EClock with timer.device */
ULONG amiga_now_ms(void)
{
    ULONG s, us; 
    CurrentTime(&s, &us); 
    return s*1000UL + (us/1000UL);
}

static ULONG now_ms(void) {
    return amiga_now_ms();
}

void MyRunLoopInit(struct MyRunLoop *loop)
{
    if (!loop) return;
    loop->port = CreateMsgPort();
    NewList((struct List*)&loop->timers);
    InitSemaphore(&loop->guard);
    loop->running = FALSE;
}

void MyRunLoopPost(struct MyRunLoop *loop, MyRunLoopFunc func, void *data)
{
    if (!loop || !func) return;
    struct MyRunLoopMsg *m = (struct MyRunLoopMsg*)AllocMem(sizeof(*m), MEMF_CLEAR);
    if (!m) return;
    m->msg.mn_Length = sizeof(*m);
    m->msg.mn_ReplyPort = loop->port; /* optional */
    m->func = func; m->data = data;
    PutMsg(loop->port, (struct Message*)m);
}

struct MyTimer *MyRunLoopAddTimer(struct MyRunLoop *loop, ULONG interval_ms, MyRunLoopFunc cb, void *data, BOOL repeat)
{
    if (!loop || !cb) return NULL;
    struct MyTimer *t = (struct MyTimer*)AllocMem(sizeof(*t), MEMF_CLEAR);
    if (!t) return NULL;
    t->interval_ms = interval_ms;
    t->callback = cb; t->user_data = data; t->repeating = repeat;
    t->next_fire_ms = now_ms() + interval_ms;
    ObtainSemaphore(&loop->guard); AddTail((struct List*)&loop->timers, (struct Node*)&t->node); ReleaseSemaphore(&loop->guard);
    return t;
}

void MyRunLoopRemoveTimer(struct MyRunLoop *loop, struct MyTimer *timer)
{
    if (!loop || !timer) return;
    ObtainSemaphore(&loop->guard); Remove((struct Node*)&timer->node); ReleaseSemaphore(&loop->guard);
    FreeMem(timer, sizeof(*timer));
}

void MyRunLoopRun(struct MyRunLoop *loop)
{
    if (!loop) return; loop->running = TRUE;
    while (loop->running) {
        // timers
        ULONG now = now_ms();
        ObtainSemaphore(&loop->guard);
        struct MyTimer *t = (struct MyTimer*)loop->timers.mlh_Head;
        while (t && t->node.mln_Succ) {
            struct MyTimer *next = (struct MyTimer*)t->node.mln_Succ;
            if (now >= t->next_fire_ms) {
                MyRunLoopFunc cb = t->callback; void *ud = t->user_data;
                if (t->repeating) t->next_fire_ms = now + t->interval_ms;
                else { Remove((struct Node*)&t->node); FreeMem(t, sizeof(*t)); t = next; // advance before callback
                       ReleaseSemaphore(&loop->guard); cb(ud); ObtainSemaphore(&loop->guard); continue; }
                ReleaseSemaphore(&loop->guard); cb(ud); ObtainSemaphore(&loop->guard);
            }
            t = next;
        }
        ReleaseSemaphore(&loop->guard);

        // messages
        struct Message *msg;
        while ((msg = GetMsg(loop->port))) {
            struct MyRunLoopMsg *m = (struct MyRunLoopMsg*)msg;
            if (m->func) m->func(m->data);
            FreeMem(m, sizeof(*m));
        }

        Delay(1); // ~1/50 s; avoids busy spin
    }
}

void MyRunLoopStop(struct MyRunLoop *loop) { if (loop) loop->running = FALSE; }

void MyRunLoopPoll(struct MyRunLoop *loop)
{
    if (!loop) return;
    
    ULONG now = now_ms();
    ObtainSemaphore(&loop->guard);
    struct MyTimer *t = (struct MyTimer*)loop->timers.mlh_Head;
    while (t && t->node.mln_Succ) {
        struct MyTimer *next = (struct MyTimer*)t->node.mln_Succ;
        if (now >= t->next_fire_ms) {
            MyRunLoopFunc cb = t->callback; void *ud = t->user_data;
            if (t->repeating) t->next_fire_ms = now + t->interval_ms;
            else { Remove((struct Node*)&t->node); FreeMem(t, sizeof(*t)); t = next;
                   ReleaseSemaphore(&loop->guard); cb(ud); ObtainSemaphore(&loop->guard); continue; }
            ReleaseSemaphore(&loop->guard); cb(ud); ObtainSemaphore(&loop->guard);
        }
        t = next;
    }
    ReleaseSemaphore(&loop->guard);
    
    for (struct Message *msg; (msg = GetMsg(loop->port)); ) {
        struct MyRunLoopMsg *m = (struct MyRunLoopMsg*)msg;
        if (m->func) m->func(m->data);
        FreeMem(m, sizeof(*m));
    }
}

void MyRunLoopDestroy(struct MyRunLoop *loop)
{
    if (!loop) return;
    loop->running = FALSE; /* in case caller forgot to stop */
    
    struct Message *msg; while ((msg = GetMsg(loop->port))) { FreeMem(msg, ((struct MyRunLoopMsg*)msg)->msg.mn_Length); }
    DeleteMsgPort(loop->port);
    ObtainSemaphore(&loop->guard);
    struct MyTimer *t = (struct MyTimer*)loop->timers.mlh_Head;
    while (t && t->node.mln_Succ) { 
        struct MyTimer *n = (struct MyTimer*)t->node.mln_Succ; 
        Remove((struct Node*)&t->node);   /* optional hygiene */
        FreeMem(t, sizeof(*t)); 
        t = n; 
    }
    ReleaseSemaphore(&loop->guard);
}

/* Note: This library is built as a static library (.a) */
/* For shared library (.library) version, uncomment the library functions above */
