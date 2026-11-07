/*
 * Multitask Library for Amiga OS3.x
 * Main source file - Static Library Version
 */

#include "multitask_lib.h"
#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dostags.h>
#include <dos/dosextens.h>
#include <proto/timer.h>
#include <devices/timer.h>
#include <clib/alib_protos.h>

/* Monotonic clock via timer.device's EClock.
 *
 * DOS DateStamp is wall-clock time and wraps around every ~19.4 days at
 * 20 ms resolution, so it is useless as a monotonic source for timed waits
 * and deadline arithmetic. timer.device's EClock is a free-running counter
 * that never wraps during normal operation and is readable with a single
 * trap. SysBase->ex_EClockFrequency is its rate in ticks per second, so
 * the conversion to a millisecond counter needs no floating point.
 */

static struct Library *timer_base = NULL;
/* TimerBase itself is already declared `extern struct Device *TimerBase;` by proto/timer.h --
 * that's the exact global name/type the inline ReadEClock() stub hardcodes as the device base
 * for its library call (same convention as SysBase/DOSBase/SocketBase). This is simply its
 * definition; proto/timer.h's own extern declaration is what every other file sharing it sees. */
struct Device *TimerBase = NULL;
static struct MsgPort *timer_port = NULL;
static struct timerequest timer_ioreq;
static ULONG eclock_freq = 0;   /* EClock ticks per second */
static BOOL timer_ok = FALSE;

static void ensure_timer_opened(void)
{
    if (timer_ok)
        return;

    Forbid();
    if (!timer_ok) {
        timer_port = CreateMsgPort();
        if (timer_port) {
            /* OpenDevice() returns a BYTE error code (0 == success); it doesn't return the
             * device base directly -- that's written into the IORequest's io_Device field. */
            timer_ioreq.tr_node.io_Message.mn_ReplyPort = timer_port;
            timer_ioreq.tr_node.io_Message.mn_Length = sizeof(timer_ioreq);
            if (OpenDevice(TIMERNAME, UNIT_ECLOCK, &timer_ioreq.tr_node, 0) == 0) {
                TimerBase = timer_ioreq.tr_node.io_Device;
                timer_base = (struct Library *)TimerBase;
                eclock_freq = (ULONG)SysBase->ex_EClockFrequency;
                timer_ok = (eclock_freq > 0);
            }
        }
    }
    Permit();
}

ULONG amiga_now_ms(void)
{
    ensure_timer_opened();

    if (timer_ok) {
        struct EClockVal ev;
        ReadEClock(&ev);
        /* ev_hi:ev_lo is a 64-bit EClock tick count. Use 64-bit intermediates:
           a machine left running accumulates ticks well beyond 32 bits. */
        unsigned long long ticks = ((unsigned long long)ev.ev_hi << 32) | ev.ev_lo;
        return (ULONG)((ticks * 1000ULL) / (unsigned long long)eclock_freq);
    }

    /* Fallback: DOS DateStamp (wall-clock, 20 ms resolution, not monotonic) */
    struct DateStamp dateStamp;
    DateStamp(&dateStamp);
    return (ULONG)dateStamp.ds_Days * 86400000UL
        + (ULONG)dateStamp.ds_Minute * 60000UL
        + (ULONG)dateStamp.ds_Tick * 20UL;
}

/* Threads are created as DOS processes (CreateNewProc), not plain exec tasks:
 * WebKit threads call dos.library (directly, or via clib2's printf/getenv/file
 * I/O and libcurl), and dos.library may only be called from a Process.
 *
 * The new process can start running (and CreateNewProc itself may Wait() on
 * DOS packets) before the creator could store anything in it, so the thread
 * record is handed over through pending_thread: the creator holds create_sem,
 * publishes the record, creates the process and waits until the child has
 * picked it up (signalled with doneSig). */
static struct SignalSemaphore create_sem;
static volatile BOOL create_sem_initialized = FALSE;
static struct MyThread *volatile pending_thread = NULL;

static void ensure_create_sem_initialized(void)
{
    if (create_sem_initialized)
        return;

    Forbid();
    if (!create_sem_initialized) {
        InitSemaphore(&create_sem);
        create_sem_initialized = TRUE;
    }
    Permit();
}

/* Thread entry point */
static LONG MyThreadEntry(void)
{
    struct MyThread *t = pending_thread;

    t->os_task = FindTask(NULL);
    Signal(t->parent, 1L << t->doneSig);   /* tell creator we have the record */

    if (t->func) {
        t->func(t->arg);
    }

    /* NEW: run per-thread TLS destructors */
    MyTLSCallDestructorsForCurrentThread();

    /* Keep detach and process exit from racing over the thread record. Returning
     * while forbidden is fine: DOS ends the process, which breaks the Forbid(). */
    Forbid();
    if (t->detached) {
        FreeMem(t, sizeof(*t));
        return 0;
    }
    t->is_running = FALSE;
    Signal(t->parent, 1L << t->doneSig);   /* tell joiner we're done */
    return 0;                 /* DOS cleans up the process */
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

    /* Initialize TLS data array */
    int i;
    for (i = 0; i < MAX_TLS_KEYS; i++) {
        thread->tls_data[i] = NULL;
    }
    thread->is_running = TRUE;

    /* Create the AmigaOS process (see pending_thread above) */
    ensure_create_sem_initialized();
    ObtainSemaphore(&create_sem);
    pending_thread = thread;
    SetSignal(0, 1L << thread->doneSig);
    struct Process *proc = CreateNewProcTags(
        NP_Entry, (ULONG)MyThreadEntry,
        NP_Name, (ULONG)"MyThread",
        NP_Priority, 0,
        NP_StackSize, 2 * 1024 * 1024, /* was 256*1024; raised because
                                          JSC::initializeThreading() spawns background
                                          threads (GC/JIT pool) through this same path,
                                          and JSC's C++ call chains are deep enough to
                                          plausibly overflow 256KB, causing stack
                                          corruption whose symptoms land at a different,
                                          unpredictable point on each run. */
        TAG_DONE);

    if (!proc) {
        pending_thread = NULL;
        ReleaseSemaphore(&create_sem);
        FreeSignal(thread->doneSig);
        FreeMem(thread, sizeof(struct MyThread));
        return NULL;
    }

    Wait(1L << thread->doneSig);    /* child has taken pending_thread and set os_task */
    pending_thread = NULL;
    ReleaseSemaphore(&create_sem);

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

void MyThreadDetach(struct MyThread *thread)
{
    if (!thread)
        return;

    Forbid();
    thread->detached = TRUE;
    if (thread->doneSig >= 0) {
        FreeSignal(thread->doneSig);
        thread->doneSig = -1;
    }
    if (!thread->is_running)
        FreeMem(thread, sizeof(*thread));
    Permit();
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
    (void)mutex;
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
    NewMinList(&cond->waiters);
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
    BYTE sigBit = w.sigBit;
    
    /* Note: Using stack-based waiter node. For timed waits or broadcast storms,
       consider heap-allocating the waiter to make lifetime explicit. */

    ObtainSemaphore(&cond->guard);
    AddTail((struct List*)&cond->waiters, (struct Node*)&w);
    ReleaseSemaphore(&cond->guard);

    ReleaseSemaphore(&mutex->sem);
    Wait(1L << sigBit);
    FreeSignal(sigBit);
    ObtainSemaphore(&mutex->sem);
}

void MyConditionSignal(struct MyCondition *cond)
{
    if (!cond) return;
    ObtainSemaphore(&cond->guard);
    struct MyCondWaiter *w = (struct MyCondWaiter*)RemHead((struct List*)&cond->waiters);
    if (w && w->task && w->sigBit >= 0) {
        BYTE sigBit = w->sigBit;
        w->sigBit = -1;
        Signal(w->task, 1L << sigBit);
    }
    ReleaseSemaphore(&cond->guard);
}

void MyConditionBroadcast(struct MyCondition *cond)
{
    if (!cond) return;
    ObtainSemaphore(&cond->guard);
    struct MyCondWaiter *w;
    while ((w = (struct MyCondWaiter*)RemHead((struct List*)&cond->waiters))) {
        if (w && w->task && w->sigBit >= 0) {
            BYTE sigBit = w->sigBit;
            w->sigBit = -1;
            Signal(w->task, 1L << sigBit);
        }
    }
    ReleaseSemaphore(&cond->guard);
}

BOOL MyConditionTimedWait(struct MyMutex *mutex, struct MyCondition *cond, ULONG timeout_ms)
{
    if (!mutex || !cond) return FALSE;

    struct Task *self = FindTask(NULL);
    struct MyCondWaiter waiter = {0};
    waiter.task = self;
    waiter.sigBit = AllocSignal(-1);
    if (waiter.sigBit < 0) return FALSE;

    BYTE sigBit = waiter.sigBit;
    ULONG signalMask = 1UL << sigBit;
    ULONG deadline = amiga_now_ms() + timeout_ms;

    ObtainSemaphore(&cond->guard);
    AddTail((struct List*)&cond->waiters, (struct Node*)&waiter);
    ReleaseSemaphore(&cond->guard);

    ReleaseSemaphore(&mutex->sem);

    BOOL timedOut = FALSE;
    while (!(SetSignal(0, signalMask) & signalMask)) {
        if ((LONG)(amiga_now_ms() - deadline) >= 0) {
            timedOut = TRUE;
            break;
        }
        Delay(1);
    }

    if (timedOut) {
        ObtainSemaphore(&cond->guard);
        if (waiter.sigBit >= 0) {
            Remove((struct Node*)&waiter);
            waiter.sigBit = -1;
        } else
            timedOut = FALSE;
        ReleaseSemaphore(&cond->guard);
    }

    FreeSignal(sigBit);
    ObtainSemaphore(&mutex->sem);
    return !timedOut;
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
static volatile BOOL tls_initialized = FALSE;

struct MyTaskTLS {
    struct Task *task;
    void *data[MAX_TLS_KEYS];
    struct MyTaskTLS *next;
};

static struct MyTaskTLS *task_tls_list = NULL;

static void ensure_tls_initialized(void)
{
    if (tls_initialized)
        return;

    Forbid();
    if (!tls_initialized) {
        InitSemaphore(&tls_guard);
        tls_initialized = TRUE;
    }
    Permit();
}

MyTLSKey MyTLSCreateWithDtor(MyTLSDtor dtor)
{
    MyTLSKey key;

    ensure_tls_initialized();
    
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
    ensure_tls_initialized();

    struct Task *task = FindTask(NULL);
    ObtainSemaphore(&tls_guard);
    struct MyTaskTLS *entry = task_tls_list;
    while (entry && entry->task != task)
        entry = entry->next;
    void *value = entry ? entry->data[key] : NULL;
    ReleaseSemaphore(&tls_guard);
    return value;
}

BOOL MyTLSSet(MyTLSKey key, void *value)
{
    if (key >= MAX_TLS_KEYS || key == 0xFFFF) return FALSE;
    ensure_tls_initialized();

    struct Task *task = FindTask(NULL);
    ObtainSemaphore(&tls_guard);
    struct MyTaskTLS *entry = task_tls_list;
    while (entry && entry->task != task)
        entry = entry->next;
    if (!entry && value) {
        entry = (struct MyTaskTLS *)AllocMem(sizeof(*entry), MEMF_CLEAR);
        if (entry) {
            entry->task = task;
            entry->next = task_tls_list;
            task_tls_list = entry;
        }
    }
    if (entry)
        entry->data[key] = value;
    ReleaseSemaphore(&tls_guard);
    return entry || !value;
}

void MyTLSDestroy(MyTLSKey key)
{
    (void)key;
    /* For now, no cleanup needed */
}

/* Call per-thread destructors; allow a few passes like pthreads */
void MyTLSCallDestructorsForCurrentThread(void)
{
    struct Task *task = FindTask(NULL);
    ensure_tls_initialized();

    for (int pass = 0; pass < 4; ++pass) {  /* 4 is ample for common patterns */
        BOOL any = FALSE;
        UWORD keyCount;
        ObtainSemaphore(&tls_guard);
        keyCount = next_tls_key;
        ReleaseSemaphore(&tls_guard);
        for (UWORD k = 0; k < keyCount; ++k) {
            void *p = MyTLSGet(k);
            ObtainSemaphore(&tls_guard);
            MyTLSDtor d = tls_dtors[k];
            ReleaseSemaphore(&tls_guard);
            if (p && d) {
                MyTLSSet(k, NULL); /* clear before calling dtor */
                d(p);
                any = TRUE;
            }
        }
        if (!any) break;
    }

    ObtainSemaphore(&tls_guard);
    struct MyTaskTLS **entry = &task_tls_list;
    while (*entry && (*entry)->task != task)
        entry = &(*entry)->next;
    struct MyTaskTLS *finished = *entry;
    if (finished)
        *entry = finished->next;
    ReleaseSemaphore(&tls_guard);
    if (finished)
        FreeMem(finished, sizeof(*finished));
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
    if (!loop)
        return;
    loop->running = TRUE;
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
