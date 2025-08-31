/*
 * Test program for Amiga Multitask Library
 * Demonstrates thread creation, mutex usage, and message passing
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "multitask_lib.h"

/* Test function prototypes */
void test_condvar(void);
void test_runloop(void);

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
