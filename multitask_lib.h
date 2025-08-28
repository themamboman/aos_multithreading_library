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
#include <libraries/dos.h>

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

/* Thread structure */
struct MyThread {
    struct Task *os_task;
    void *user_data;
    struct Node node;  /* For linking threads */
    BOOL is_running;
};

/* Mutex structure */
struct MyMutex {
    struct SignalSemaphore sem;
};

/* Message port structure */
struct MyMessagePort {
    struct MsgPort port;
    BYTE signal;
};

/* Function prototypes */
typedef void (*ThreadFunc)(void *arg);

/* Library functions */
struct MyThread *MyThreadCreate(ThreadFunc func, void *arg);
void MyThreadDestroy(struct MyThread *thread);
BOOL MyThreadIsRunning(struct MyThread *thread);
void MyThreadWait(struct MyThread *thread);

void MyMutexInit(struct MyMutex *mutex);
void MyMutexLock(struct MyMutex *mutex);
void MyMutexUnlock(struct MyMutex *mutex);
void MyMutexDestroy(struct MyMutex *mutex);

struct MyMessagePort *MyPortCreate(void);
void MyPortDestroy(struct MyMessagePort *my_port);
struct Message *MyPortWaitMessage(struct MyMessagePort *my_port);
BOOL MyPortPostMessage(struct MyMessagePort *my_port, struct Message *msg);

/* Library open/close functions */
struct MultitaskBase *MultitaskOpen(void);
void MultitaskClose(struct MultitaskBase *base);

#endif /* MULTITASK_LIB_H */
