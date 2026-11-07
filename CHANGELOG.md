# Multitask Library Changelog

## Overview
This document tracks changes to the AmigaOS 3.x multitask library.

## 2026-11: fixes for OdysseyWebBrowser on AmigaOS 3.x

Found while bringing up [OdysseyWebBrowser for AmigaOS 3.x](https://github.com/themamboman/OdysseyWebBrowser-m68k).
The browser now runs on this library under WinUAE.

- **Threads are DOS processes**: `MyThreadCreate` uses `CreateNewProc` instead of `CreateTask`,
  because WebKit threads call dos.library through the C library and libcurl.
- **Safer start-up handoff**: the thread record is passed to the new process under a semaphore,
  and the creator waits until the child has taken it.
- **2 MB thread stacks** (was 256 KB) for JavaScriptCore.
- **`MyThreadDetach()`**: release a thread without joining; the record is freed when it ends.
- **`MyConditionTimedWait()`**: condition wait with a timeout in milliseconds.
- **TLS for any task**: `MyTLSSet` keeps per-task storage for tasks the library did not create,
  such as the main task, and now returns `BOOL`.
- **Monotonic clock**: `amiga_now_ms()` reads timer.device's EClock instead of DOS `DateStamp`.
  `ex_EClockFrequency` is ticks per second; an earlier version used it as microseconds per tick,
  so the clock ran about 500,000 times too fast on PAL and timed waits expired at once.
- **Tests**: `test_program.c` adds clock, condition-timeout, task-local TLS and detached-thread
  tests (built, not yet run on their own).

The sections below are earlier development notes. Their status claims predate any target build.

## Historical Implementation Notes

### Historical Threading Implementation Notes
- **Race-free thread creation**: `Forbid()/Permit()` wrapper around `CreateTask` + `tc_UserData` assignment
- **TLS destructors**: `MyTLSCreateWithDtor()`, `MyTLSCallDestructorsForCurrentThread()` called at thread exit
- **Proper join semantics**: `MyThreadWait()` before `MyThreadDestroy()` with signal-based synchronization
- **Volatile state flags**: `is_running` marked as `volatile` for proper memory visibility
- **TLS key exhaustion**: Returns `0xFFFF` when `MAX_TLS_KEYS` (32) is exceeded

### Networking Stub - HTTP Client
- **Complete error handling**: 8 distinct error codes (-1 to -8) covering all failure modes
- **Host header truncation protection**: Validates `hostLine` length before injection
- **Receive error distinction**: Separates timeout/error (-8) from clean EOF (0 bytes)
- **EINTR-safe I/O**: Robust `recv` loop with proper retry logic
- **Partial send detection**: Returns -7 if full request isn't transmitted
- **Post-cleanup error reporting**: `http_last_errno()` caches errors before `SocketBase` cleanup
- **HTTP headers**: Host:port formatting, Accept: */*, Accept-Encoding: identity, Connection: close, User-Agent
- **HTTP parsing helpers**: `http_parse_response()`, `http_parse_status_and_length()`
- **HTTP/1.1 support**: Full protocol compliance with chunked transfer encoding
- **Chunked dechunker**: In-place processing of chunked responses

### Memory Management
- **Per-thread destructors**: Automatic cleanup of TLS objects at thread exit
- **Resource cleanup**: Consistent `SocketBase = NULL` on all error paths
- **Timer node cleanup**: `Remove()` before `FreeMem()` in `MyRunLoopDestroy`
- **Condition variable hygiene**: Proper empty-list checks for `MinList`

### Build System
- **Warning flags**: `-Wall -Wextra` enabled for early bug detection
- **Header hygiene**: Removed unused `<libraries/dos.h>` dependency
- **Socket library linking**: `-lsocket` added for proper bsdsocket stubs
- **Clean targets**: Comprehensive cleanup of all build artifacts

## Detailed Change History

### **Phase 1: Core Correctness Fixes**
- Fixed header order bug: `ThreadFunc` and `MAX_TLS_KEYS` moved before `struct MyThread`
- Implemented TLS key exhaustion check in `MyTLSCreate()`
- Added proper thread joining in test program (`MyThreadWait` before `MyThreadDestroy`)
- Added `loop->running = FALSE` guard in `MyRunLoopDestroy()`
- Fixed MinList empty check: `mlh_Head == &mlh_Tail` (not `&mlh_TailPred`)

### **Phase 2: TLS Destructor System**
- Added `typedef void (*MyTLSDtor)(void*);` function type
- Implemented `MyTLSCreateWithDtor(MyTLSDtor dtor)` for WTF compatibility
- Added `MyTLSCallDestructorsForCurrentThread()` called at thread exit
- Hooked destructor system into `MyThreadEntry` before `RemTask(NULL)`

### **Phase 3: Thread Creation Race Fix**
- Wrapped `CreateTask()` and `tc_UserData` assignment in `Forbid()/Permit()`
- Ensures `tc_UserData` is set before task can run `MyThreadEntry`
- Prevents crash from accessing uninitialized thread context

### **Phase 4: Networking Stub Implementation**
- Replaced `RawDoFmt` with `snprintf` for HTTP request construction
- Added professional HTTP headers (Host:port, Accept: */*, Connection: close, User-Agent)
- Implemented robust send/recv loops with EINTR handling
- Added 5-second receive timeout via `SO_RCVTIMEO`
- Implemented numeric host fast-path with `inet_aton()`
- Added graceful shutdown with `shutdown(SHUT_RDWR)`

### **Phase 5: Error Handling & Robustness**
- Added buffer parameter validation (error -5)
- Added request formatting error detection (error -6)
- Added partial send detection (error -7)
- Added receive timeout/error distinction (error -8)
- Implemented `http_last_errno()` with cached error values
- Added defensive `SocketBase` cleanup on all error paths

### **Phase 6: HTTP Parsing Helpers**
- `http_parse_response()`: Splits headers and body with lenient fallback
- `http_parse_status_and_length()`: Extracts status code, content-length, chunked flag
- `ci_starts_with()`: Case-insensitive string comparison utility
- Proper CRLF handling for both `\r\n\r\n` and `\n\n` terminators

### **Phase 7: Final Polish & Edge Cases**
- **Host header truncation protection**: Validates `hostLine` length before use
- **Receive error distinction**: Separates timeout (-8) from clean EOF (0)
- **Concurrency documentation**: Clear warnings about global state limitations
- **Build warnings**: `-Wall -Wextra` for development quality
- **Header hygiene**: Removed unused dependencies

### **Phase 8: Error Handling**
- **Named error constants**: `HTTP_ERR_*` defines for cleaner code and tests
- **Host validation guard**: Treats empty host as resolve failure
- **EINTR handling in send loop**: Symmetric with recv loop for robustness
- **Future TLS notes**: Documentation for concurrent load implementation
- **Error code consistency**: All return paths use named constants

### **Phase 9: HTTP/1.1 & Chunked Transfer Support**
- **HTTP/1.1 protocol**: Upgraded from HTTP/1.0 for modern server compatibility
- **Chunked transfer encoding**: Full support for HTTP/1.1 chunked bodies
- **In-place dechunker**: `http_dechunk_inplace()` handles chunked responses efficiently
- **Compression avoidance**: `Accept-Encoding: identity` prevents gzip/deflate issues
- **Enhanced test program**: Demonstrates chunked dechunking and content-length handling

## Error Code Reference

| Code | Constant | Meaning | Recovery Action |
|------|----------|---------|-----------------|
| -1 | `HTTP_ERR_OPENLIB` | Failed to open bsdsocket.library | Check library availability |
| -2 | `HTTP_ERR_SOCKET` | Failed to create socket | Check system resources |
| -3 | `HTTP_ERR_RESOLVE` | Failed to resolve hostname | Check DNS/network connectivity |
| -4 | `HTTP_ERR_CONNECT` | Failed to connect | Check server availability/firewall |
| -5 | `HTTP_ERR_BADBUF` | Invalid buffer parameters | Validate input parameters |
| -6 | `HTTP_ERR_FORMAT` | Request formatting error | Check hostname length/format |
| -7 | `HTTP_ERR_PARTIALSEND` | Partial send / write error | Check network stability |
| -8 | `HTTP_ERR_RECV` | Receive timeout / read error | Check server response time |

## WTF Integration Checklist

### Memory Management
- [x] Per-thread destructors with automatic cleanup
- [x] Race-free thread start with proper synchronization  
- [x] Proper join semantics matching WTF expectations
- [x] TLS key exhaustion handling with validation
- [x] Atomic operations with minimal critical sections
- [x] RunLoop/Timer system ready for WTF mapping
- [x] Post-cleanup error reporting via cached errno

### Networking
- [x] Host header truncation protection
- [x] Receive error distinction (timeout vs EOF)
- [x] Comprehensive error codes (-1 to -8)
- [x] EINTR-safe I/O with proper retry logic
- [x] Partial send detection with specific error reporting
- [x] Consistent resource cleanup on all code paths
- [x] Concurrency documentation for future multi-threading

### Threading Primitives
- [x] Thread creation/destruction with proper lifecycle
- [x] Mutex implementation using `SignalSemaphore`
- [x] Condition variables with waiter management
- [x] Thread-local storage with destructor support
- [x] Atomic operations using `Forbid()/Permit()`
- [x] Run loop with timer and message pump support

## Build Environment Notes

### **AmigaOS 3.x (VBCC) - Library Building**
- **Compiler**: `vc` (vbcc)
- **Flags**: `-O2 -DAMIGA -I. -Wall -Wextra`
- **Libraries**: `-lamiga -lsocket`
- **Output**: `libmultitask.a` (static library)
- **Target**: AmigaOS 3.x native compilation

### **Linux (GCC Cross-Compiler)**
- **Compiler**: `m68k-amigaos-gcc` (cross-compiler)
- **Target**: AmigaOS 3.x from Linux host
- **Integration**: Link `libmultitask.a` into WTF/WebCore
- **Architecture**: Single-process WebKit1 style

## File Structure

```
aos_multithreading_library/
├── multitask_lib.h          # Main library header
├── multitask_lib.c          # Core implementation
├── amiga_networking.h       # Networking API header
├── amiga_networking.c       # HTTP client implementation
├── test_program.c           # Threading test program
├── test_networking.c        # Networking test program
├── ThreadingAmiga.cpp       # WTF integration skeleton
├── Makefile                 # Build configuration
├── CHANGELOG.md             # This file
└── WEBKIT_INTEGRATION_STEPS.md  # WebKit integration guide
```

## Next Steps for WebKit Integration

1. **Freeze snapshot**: Pick older, lean WebKit (or OWB-era)
2. **Add Amiga platform**: Introduce `OS(AMIGA)` or `PLATFORM(AMIGA)`
3. **Implement WTF mappings**: Use `ThreadingAmiga.cpp` as starting point
4. **Stub networking**: Wire `http_get()` into WebCore's sync loader
5. **Graphics stub**: Implement RTG bitmap blitting for rendering
6. **Single-process architecture**: Keep UI + engine in one task initially

## Concurrency Limitations & Solutions

### **Current Limitations**
- `http_last_errno()` is global - read immediately after call
- `SocketBase` is global - only one task should use sockets at a time
- No built-in multi-threading support for networking

### **Future Solutions**
- **Short-term**: Document as single-threaded, read errors immediately
- **Medium-term**: Centralize socket I/O on one "network" thread
- **Advanced**: Make SocketBase per-thread using TLS + wrapper functions

## Quality Assurance

### **Testing Coverage**
- ✅ Thread creation/destruction lifecycle
- ✅ TLS key allocation and cleanup
- ✅ Condition variable signaling
- ✅ Run loop timer and message handling
- ✅ HTTP GET with various error conditions
- ✅ HTTP response parsing edge cases
- ✅ Error code propagation and reporting

### **Design Goals**
- ✅ No race conditions in thread creation
- ✅ No resource leaks or stale handles
- ✅ Comprehensive error handling and reporting
- ✅ Professional HTTP client implementation
- ✅ WTF-compatible memory management semantics
- ✅ Build system with development warnings
