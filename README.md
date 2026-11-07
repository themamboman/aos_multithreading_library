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

There are two ways to build it.

### Inside a larger project (cross-compiled)

This is how OdysseyWebBrowser for AmigaOS 3.x uses it: `multitask_lib.c` is compiled straight into
the project, with no separate library step. Tested with the AmigaPorts `m68k-amigaos-gcc`
(GCC 16) and clib2:

```bash
m68k-amigaos-gcc -mcpu=68040 -fbaserel -fno-pic -mcrt=clib2 -O1 -I. -c multitask_lib.c
```

Add this directory to the include path so `multitask_lib.h` is found.

### Static library with vbcc (Makefile)

The Makefile builds `libmultitask.a` with vbcc (`vc`). It has not been re-tested since the
changes for the browser port.

```bash
make                # build libmultitask.a
make test_program   # build the threading test program
make clean          # remove build files
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
- `MyThreadDetach(thread)` - Release a thread handle without joining; call from its creating task

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

### Condition and TLS Functions

- `MyConditionTimedWait(mutex, condition, timeout_ms)` - Wait up to the relative timeout in milliseconds
- `MyTLSSet(key, value)` - Set task-local data; returns `FALSE` if the key is invalid or storage cannot be allocated

`MyTLSSet` supports both tasks created by this library and existing Amiga tasks. The task-local storage limit is currently `MAX_TLS_KEYS` (32).

## Library Management

- `MultitaskOpen()` - Open the library
- `MultitaskClose(base)` - Close the library

## Notes

- This library is built on top of Amiga's native Exec kernel
- Threads are created as DOS processes (`CreateNewProc`) with a 2 MB stack, so they can call
  dos.library and C library I/O
- `amiga_now_ms()` reads timer.device's EClock, falling back to DOS `DateStamp`
- No memory protection - all threads share the same address space
- Use mutexes to protect shared data
- Message ports provide safe inter-task communication

## Testing

Build and run the test program on an AmigaOS 3.x target (or compatible emulator):

```bash
make test_program
./test_program
```

The test program demonstrates:
- Creating multiple worker threads
- Using mutexes for synchronization
- Message passing between threads
- Condition signaling and timed-out waits
- Task-local TLS isolation and destructor calls
- Repeated detached-thread cleanup

The library itself runs inside OdysseyWebBrowser on AmigaOS 3.x (tested under WinUAE), which
exercises threads, mutexes, conditions, timed waits and TLS. `test_program` has been built but
not yet run on its own.

## Used by

- [OdysseyWebBrowser for AmigaOS 3.x (m68k)](https://github.com/themamboman/OdysseyWebBrowser-m68k):
  WebKit's WTF threading layer (`Source/WTF/wtf/amiga/ThreadingAmiga.cpp`) maps threads, locks,
  conditions and thread-local storage onto this library.

## License

This library is provided as-is for educational and development purposes.
