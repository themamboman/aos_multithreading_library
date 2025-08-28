/*
 * Test program for Amiga Multitask Library
 * Demonstrates thread creation, mutex usage, and message passing
 */

#include <stdio.h>
#include <stdlib.h>
#include "multitask_lib.h"

/* Shared data structure */
struct SharedData {
    int counter;
    struct MyMutex mutex;
    struct MyMessagePort *port;
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
        printf("Received message: %s\n", (char *)msg->mn_Node.ln_Name);
        ReplyMsg(msg);
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
        
        if (MyPortPostMessage(shared_data.port, test_msg)) {
            printf("Message posted successfully\n");
        } else {
            printf("Failed to post message\n");
        }
        
        /* Wait a bit for message to be processed */
        Delay(10);
        
        FreeMem(test_msg, sizeof(struct Message));
    }
    
    /* Clean up */
    printf("\nCleaning up...\n");
    
    MyThreadDestroy(worker1);
    MyThreadDestroy(worker2);
    MyThreadDestroy(msg_handler);
    
    MyMutexDestroy(&shared_data.mutex);
    MyPortDestroy(shared_data.port);
    
    printf("Test completed successfully!\n");
    
    return 0;
}
