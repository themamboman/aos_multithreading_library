# Amiga Multitask Library

A basic multitasking library for Amiga OS3.x that provides thread management, synchronization primitives, and inter-task communication.

## Features

- **Thread Management**: Create, destroy, and manage threads (tasks)
- **Synchronization**: Mutex support using Amiga's SignalSemaphore
- **Message Passing**: Inter-task communication via message ports
- **Amiga Native**: Built on top of Amiga's Exec kernel

## Files

- `multitask_lib.h` - Header file with function declarations
- `multitask_lib.c` - Main library implementation
- `multitask_stubs.c` - Library stubs for linking
- `test_program.c` - Example program demonstrating usage
- `Makefile` - Build configuration

## Building

### Prerequisites

- vbcc compiler (Amiga C compiler)
- Amiga SDK headers
- Make utility

### Build Commands

```bash
# Build the library
make

# Build stubs only
make stubs

# Build test program
make test_program

# Clean build files
make clean

# Install library (requires root access)
make install
```

## Usage

### Basic Thread Creation

```c
#include "multitask_lib.h"

void worker_function(void *arg) {
    printf("Worker thread running\n");
    // Your thread code here
}

int main() {
    struct MyThread *thread = MyThreadCreate(worker_function, NULL);
    if (thread) {
        MyThreadWait(thread);  // Wait for completion
        MyThreadDestroy(thread);
    }
    return 0;
}
```

### Using Mutexes

```c
struct MyMutex mutex;
MyMutexInit(&mutex);

MyMutexLock(&mutex);
// Critical section - only one thread can execute this
MyMutexUnlock(&mutex);

MyMutexDestroy(&mutex);
```

### Message Passing

```c
struct MyMessagePort *port = MyPortCreate();
struct Message *msg = /* create message */;

// Send message
MyPortPostMessage(port, msg);

// Receive message
msg = MyPortWaitMessage(port);

MyPortDestroy(port);
```

## API Reference

### Thread Functions

- `MyThreadCreate(func, arg)` - Create a new thread
- `MyThreadDestroy(thread)` - Destroy a thread
- `MyThreadIsRunning(thread)` - Check if thread is active
- `MyThreadWait(thread)` - Wait for thread completion

### Mutex Functions

- `MyMutexInit(mutex)` - Initialize a mutex
- `MyMutexLock(mutex)` - Acquire lock
- `MyMutexUnlock(mutex)` - Release lock
- `MyMutexDestroy(mutex)` - Clean up mutex

### Message Port Functions

- `MyPortCreate()` - Create a message port
- `MyPortDestroy(port)` - Destroy a message port
- `MyPortWaitMessage(port)` - Wait for messages
- `MyPortPostMessage(port, msg)` - Send a message

## Library Management

- `MultitaskOpen()` - Open the library
- `MultitaskClose(base)` - Close the library

## Notes

- This library is built on top of Amiga's native Exec kernel
- Threads are implemented as Amiga tasks
- No memory protection - all threads share the same address space
- Use mutexes to protect shared data
- Message ports provide safe inter-task communication

## Testing

Run the test program to verify the library works correctly:

```bash
make test_program
./test_program
```

The test program demonstrates:
- Creating multiple worker threads
- Using mutexes for synchronization
- Message passing between threads
- Proper cleanup and resource management

## Porting to WebKit

This library provides the basic threading primitives needed for porting WebKit to Amiga OS3.x:

- **Thread Creation**: WebKit's worker threads can be implemented using `MyThreadCreate`
- **Synchronization**: WebKit's locks and mutexes can use `MyMutex` functions
- **Message Passing**: WebKit's inter-thread communication can use message ports
- **Task Management**: Proper task lifecycle management for WebKit components

## License

This library is provided as-is for educational and development purposes.
