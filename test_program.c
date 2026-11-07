/*
 * Test program for Amiga Multitask Library
 * Demonstrates thread creation, mutex usage, and message passing
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include "multitask_lib.h"

/* Test function prototypes */
void test_condvar(void);
void test_runloop(void);
BOOL test_monotonic_clock(void);
BOOL test_condition_timeout(void);
BOOL test_task_tls(void);
BOOL test_detached_threads(void);

/* Shared data structure */
struct SharedData {
    int counter;
    struct MyMutex mutex;
    MyMessagePort *port;
};

/* Worker thread function */
void worker_thread(void *arg)
{
    struct SharedData *data = (struct SharedData *)arg;
    int i;
    
    printf("Worker thread started\n");
    
    for (i = 0; i < 1000; i++) {
        MyMutexLock(&data->mutex);
        data->counter++;
        MyMutexUnlock(&data->mutex);
        
        /* Small delay to make threading visible */
        Delay(1);
    }
    
    printf("Worker thread finished, counter = %d\n", data->counter);
}

/* Message handler thread */
void message_handler(void *arg)
{
    struct SharedData *data = (struct SharedData *)arg;
    struct Message *msg;
    
    printf("Message handler thread started\n");
    
    /* Wait for messages */
    while ((msg = MyPortWaitMessage(data->port)) != NULL) {
        if (msg->mn_Node.ln_Name && !strcmp(msg->mn_Node.ln_Name, "QUIT")) {
            FreeMem(msg, msg->mn_Length);
            break;
        }
        printf("Received message: %s\n", (char *)msg->mn_Node.ln_Name);
        /* Free the message after processing to avoid race conditions */
        FreeMem(msg, msg->mn_Length);
    }
    
    printf("Message handler thread finished\n");
}

int main(void)
{
    struct SharedData shared_data;
    struct MyThread *worker1, *worker2, *msg_handler;
    struct Message *test_msg;
    
    printf("Amiga Multitask Library Test Program\n");
    printf("====================================\n\n");
    
    /* Initialize shared data */
    shared_data.counter = 0;
    MyMutexInit(&shared_data.mutex);
    shared_data.port = MyPortCreate();
    
    if (!shared_data.port) {
        printf("Failed to create message port!\n");
        return 1;
    }
    
    printf("Creating worker threads...\n");
    
    /* Create worker threads */
    worker1 = MyThreadCreate(worker_thread, &shared_data);
    worker2 = MyThreadCreate(worker_thread, &shared_data);
    msg_handler = MyThreadCreate(message_handler, &shared_data);
    
    if (!worker1 || !worker2 || !msg_handler) {
        printf("Failed to create threads!\n");
        return 1;
    }
    
    printf("Worker threads created successfully\n");
    printf("Main thread waiting for workers to complete...\n\n");
    
    /* Wait for worker threads to complete */
    MyThreadWait(worker1);
    MyThreadWait(worker2);
    
    printf("Worker threads completed\n");
    printf("Final counter value: %d\n", shared_data.counter);
    
    /* Test message passing */
    printf("\nTesting message passing...\n");
    
    test_msg = (struct Message *)AllocMem(sizeof(struct Message), MEMF_CLEAR);
    if (test_msg) {
        test_msg->mn_Node.ln_Name = "Test Message";
        test_msg->mn_Length = sizeof(struct Message);
        test_msg->mn_ReplyPort = NULL;
        
        MyPortPostMessage(shared_data.port, test_msg);
        printf("Message posted successfully\n");
        
        /* Wait a bit for message to be processed */
        Delay(10);
        
        /* Send quit message to properly terminate message handler */
        struct Message *quit = AllocMem(sizeof(*quit), MEMF_CLEAR);
        quit->mn_Node.ln_Name = "QUIT";
        quit->mn_Length = sizeof(*quit);
        MyPortPostMessage(shared_data.port, quit);
        
        /* Wait for message handler to process QUIT and exit */
        MyThreadWait(msg_handler);
        
        /* Message is freed by the handler thread - no need to free here */
    }
    
    /* Clean up */
    printf("\nCleaning up...\n");
    
    MyThreadDestroy(worker1);
    MyThreadDestroy(worker2);
    MyThreadDestroy(msg_handler);
    
    MyMutexDestroy(&shared_data.mutex);
    MyPortDestroy(shared_data.port);
    
    printf("Test completed successfully!\n");
    
    /* Test condition variables */
    printf("\nTesting condition variables...\n");
    test_condvar();
    
    /* Test run loop */
    printf("\nTesting run loop...\n");
    test_runloop();

    if (!test_monotonic_clock() || !test_condition_timeout() || !test_task_tls() || !test_detached_threads()) {
        printf("\nA new threading regression test failed!\n");
        return 1;
    }

    printf("\nAll tests completed successfully!\n");
    
    return 0;
}

/* Condition variable ping-pong test */
static struct MyMutex m;
static struct MyCondition c;
static int ready = 0;

void worker(void *arg)
{
    (void)arg; /* unused */
    MyMutexLock(&m);
    ready = 1;
    MyConditionSignal(&c);
    MyMutexUnlock(&m);
}

void test_condvar(void)
{
    struct MyThread *t;
    MyMutexInit(&m);
    MyConditionInit(&c);

    MyMutexLock(&m);
    t = MyThreadCreate(worker, NULL);
    while (!ready) MyConditionWait(&m, &c);
    MyMutexUnlock(&m);

    MyThreadWait(t);
    MyThreadDestroy(t);
    MyConditionDestroy(&c);
    MyMutexDestroy(&m);
    
    printf("Condition variable test passed\n");
}

/* Verifies amiga_now_ms() is actually a monotonic clock rather than the old
 * DOS DateStamp value. DateStamp is wall-clock time with 20 ms resolution and
 * it wraps at the day boundary, so it can run backwards across midnight and
 * is far too coarse for timeout arithmetic. This checks both failure modes:
 * monotonicity over many samples, and that a known sleep measures in the right
 * range with resolution finer than one DateStamp tick.
 */
BOOL test_monotonic_clock(void)
{
    /* 1. Take many back-to-back samples; a non-monotonic source is detected
          immediately because every sample must be >= the previous one. */
    ULONG previous = amiga_now_ms();
    int backwards = 0;
    for (int i = 0; i < 5000; ++i) {
        ULONG now = amiga_now_ms();
        if ((LONG)(now - previous) < 0)
            ++backwards;
        previous = now;
    }

    /* 2. Measure a known sleep. DateStamp's 20 ms granularity would report
          this in steps of 20; EClock measures it to sub-millisecond accuracy.
          The window is deliberately wide so this does not flake on slow
          hardware while still rejecting a stuck or wrong-scale clock. */
    ULONG start = amiga_now_ms();
    Delay(12);                          /* 12 vertical blank ticks ~= 240 ms */
    ULONG elapsed = amiga_now_ms() - start;

    /* 3. Sample again after a further delay; a wrap-around in the clock would
          make the second measurement appear smaller than the first. */
    Delay(6);
    ULONG second = amiga_now_ms() - start;
    if (second < elapsed)
        ++backwards;

    BOOL passed = (backwards == 0) && elapsed >= 150 && elapsed <= 600;
    printf("Monotonic clock test %s (backwards readings=%d, 240ms sleep measured %lu ms)\n",
           passed ? "passed" : "failed", backwards, (unsigned long)elapsed);
    return passed;
}

BOOL test_condition_timeout(void)
{
    struct MyMutex mutex;
    struct MyCondition condition;
    MyMutexInit(&mutex);
    MyConditionInit(&condition);

    MyMutexLock(&mutex);
    ULONG start = amiga_now_ms();
    BOOL signaled = MyConditionTimedWait(&mutex, &condition, 60);
    ULONG elapsed = amiga_now_ms() - start;
    MyMutexUnlock(&mutex);

    MyConditionDestroy(&condition);
    MyMutexDestroy(&mutex);
    BOOL passed = !signaled && elapsed >= 40;
    printf("Condition timeout test %s\n", passed ? "passed" : "failed");
    return passed;
}

static MyTLSKey test_tls_key;
static int main_tls_value = 7;
static int worker_tls_value = 11;
static volatile LONG tls_destructor_calls;
static volatile BOOL tls_worker_passed;

static void test_tls_destructor(void *value)
{
    if (value == &worker_tls_value)
        tls_destructor_calls++;
}

static void tls_worker(void *arg)
{
    (void)arg;
    tls_worker_passed = !MyTLSGet(test_tls_key)
        && MyTLSSet(test_tls_key, &worker_tls_value)
        && MyTLSGet(test_tls_key) == &worker_tls_value;
}

BOOL test_task_tls(void)
{
    test_tls_key = MyTLSCreateWithDtor(test_tls_destructor);
    if (test_tls_key == (MyTLSKey)0xFFFF)
        return FALSE;

    tls_destructor_calls = 0;
    tls_worker_passed = FALSE;
    if (!MyTLSSet(test_tls_key, &main_tls_value))
        return FALSE;

    struct MyThread *thread = MyThreadCreate(tls_worker, NULL);
    if (!thread)
        return FALSE;
    MyThreadWait(thread);
    MyThreadDestroy(thread);

    BOOL passed = tls_worker_passed
        && tls_destructor_calls == 1
        && MyTLSGet(test_tls_key) == &main_tls_value;
    MyTLSDestroy(test_tls_key);
    printf("Task-local TLS test %s\n", passed ? "passed" : "failed");
    return passed;
}

static volatile ULONG detached_thread_count;

static void detached_worker(void *arg)
{
    (void)arg;
    Forbid();
    detached_thread_count++;
    Permit();
}

BOOL test_detached_threads(void)
{
    detached_thread_count = 0;
    for (ULONG i = 0; i < 40; ++i) {
        struct MyThread *thread = MyThreadCreate(detached_worker, NULL);
        if (!thread)
            return FALSE;
        MyThreadDetach(thread);

        ULONG deadline = amiga_now_ms() + 2000;
        while (detached_thread_count <= i && (LONG)(amiga_now_ms() - deadline) < 0)
            Delay(1);
        if (detached_thread_count <= i)
            return FALSE;
        Delay(1);
    }
    printf("Detached thread cleanup test passed\n");
    return TRUE;
}

/* Run loop test */
static void tick(void *p) 
{ 
    printf("tick %ld\n", (long)p); 
}

static void posted_func(void *p)
{
    (void)p; /* unused */
    printf("posted!\n");
}

void test_runloop(void)
{
    struct MyRunLoop L;
    MyRunLoopInit(&L);
    MyRunLoopAddTimer(&L, 500, tick, (void*)1, TRUE); // 500ms repeating
    MyRunLoopPost(&L, posted_func, NULL);
    
    // Run a short while using non-blocking poll
    int i;
    for (i = 0; i < 100; ++i) { 
        MyRunLoopPoll(&L); 
        Delay(1); 
    }
    
    MyRunLoopDestroy(&L);
    printf("Run loop test passed\n");
}
