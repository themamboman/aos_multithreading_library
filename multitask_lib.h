/*
 * Multitask Library for Amiga OS3.x
 * Header file
 */

#ifndef MULTITASK_LIB_H
#define MULTITASK_LIB_H

#include <exec/exec.h>
#include <exec/libraries.h>
#include <exec/semaphores.h>
#include <exec/ports.h>
#include <exec/lists.h>

/* Library version */
#define MULTITASK_LIB_VERSION 1
#define MULTITASK_LIB_REVISION 0

/* Library base structure */
struct MultitaskBase {
    struct Library lib;
    UWORD version;
    UWORD revision;
    /* Add any library-specific data here */
};

/* Function prototypes - must come before struct definitions */
typedef void (*ThreadFunc)(void *arg);
#define MAX_TLS_KEYS 32

/* Thread structure */
struct MyThread {
    struct Task *os_task;
    ThreadFunc func;           /* Function to execute */
    void *arg;                 /* Argument to pass to function */
    struct Task *parent;       /* Parent task */
    BYTE doneSig;             /* Signal to signal parent when done */
    struct Node node;          /* For linking threads */
    volatile BOOL is_running;
    void *tls_data[MAX_TLS_KEYS]; /* Thread-local storage data */
    struct MyThread *next;     /* For linking in lists */
};

/* Mutex structure */
struct MyMutex {
    struct SignalSemaphore sem;
};

/* Message port structure - now just a typedef for the OS MsgPort */
typedef struct MsgPort MyMessagePort;

/* Condition variable structure */
struct MyCondWaiter {
    struct MinNode node;
    struct Task *task;
    BYTE sigBit; /* allocated via AllocSignal(-1) */
};

struct MyCondition {
    struct MinList waiters;          /* list of MyCondWaiter */
    struct SignalSemaphore guard;    /* protects waiters */
};

/* Thread-local storage key */
typedef UWORD MyTLSKey;

/* Thread-local storage destructor function type */
typedef void (*MyTLSDtor)(void*);

/* Atomic operations structure */
struct MyAtomic {
    volatile LONG value;
};

/* Run loop structure for main thread event processing */
typedef void (*MyRunLoopFunc)(void*);

struct MyRunLoopMsg {
    struct Message msg;
    MyRunLoopFunc func;
    void *data;
};

struct MyTimer {
    struct MinNode node;
    ULONG next_fire_ms;
    ULONG interval_ms;
    BOOL  repeating;
    MyRunLoopFunc callback;
    void *user_data;
};

struct MyRunLoop {
    struct MsgPort *port;
    struct MinList timers;
    struct SignalSemaphore guard;
    BOOL running;
};

/* Function prototypes */

/* Library functions */
struct MyThread *MyThreadCreate(ThreadFunc func, void *arg);
void MyThreadDestroy(struct MyThread *thread);
BOOL MyThreadIsRunning(struct MyThread *thread); /* May lag behind actual thread state */
void MyThreadWait(struct MyThread *thread);
void MyThreadYield(void); /* Yield CPU time to other tasks */

/* Note: Callers MUST call MyThreadWait() before MyThreadDestroy() */

void MyMutexInit(struct MyMutex *mutex);
void MyMutexLock(struct MyMutex *mutex);
void MyMutexUnlock(struct MyMutex *mutex);
void MyMutexDestroy(struct MyMutex *mutex);

MyMessagePort *MyPortCreate(void);
void MyPortDestroy(MyMessagePort *my_port);
struct Message *MyPortWaitMessage(MyMessagePort *my_port);
void MyPortPostMessage(MyMessagePort *my_port, struct Message *msg);

/* New primitives */
void MyConditionInit(struct MyCondition *cond);
void MyConditionWait(struct MyMutex *mutex, struct MyCondition *cond);
void MyConditionSignal(struct MyCondition *cond);
void MyConditionBroadcast(struct MyCondition *cond);
void MyConditionDestroy(struct MyCondition *cond);

/* Thread-local storage functions */
/* Note: MyTLSCreate() can fail if MAX_TLS_KEYS (32) is exceeded */
/* Failed creation returns 0xFFFF - check for this value in callers */
MyTLSKey MyTLSCreate(void);
MyTLSKey MyTLSCreateWithDtor(MyTLSDtor dtor);
void *MyTLSGet(MyTLSKey key);
void MyTLSSet(MyTLSKey key, void *value);
void MyTLSDestroy(MyTLSKey key);

/* Run all registered TLS destructors for the current thread */
void MyTLSCallDestructorsForCurrentThread(void);

void MyAtomicInit(struct MyAtomic *atomic, LONG initial_value);
LONG MyAtomicLoad(struct MyAtomic *atomic);
void MyAtomicStore(struct MyAtomic *atomic, LONG value);
void MyAtomicAdd(struct MyAtomic *atomic, LONG delta);
LONG MyAtomicCompareExchange(struct MyAtomic *atomic, LONG expected, LONG desired);

/* Run loop functions */
void MyRunLoopInit(struct MyRunLoop *loop);
void MyRunLoopRun(struct MyRunLoop *loop);
void MyRunLoopStop(struct MyRunLoop *loop);
void MyRunLoopPoll(struct MyRunLoop *loop); /* Non-blocking single iteration */
void MyRunLoopPost(struct MyRunLoop *loop, MyRunLoopFunc func, void *data);
struct MyTimer *MyRunLoopAddTimer(struct MyRunLoop *loop, ULONG interval_ms, MyRunLoopFunc cb, void *data, BOOL repeat);
void MyRunLoopRemoveTimer(struct MyRunLoop *loop, struct MyTimer *timer);
void MyRunLoopDestroy(struct MyRunLoop *loop);

/* Utility functions */
ULONG amiga_now_ms(void); /* Monotonic time in milliseconds for WTF integration */

/* Library open/close functions */
struct MultitaskBase *MultitaskOpen(void);
void MultitaskClose(struct MultitaskBase *base);

#endif /* MULTITASK_LIB_H */
