# WebKit Integration Guide for AmigaOS

This guide outlines the steps to integrate our `multitask.library` with WebKit for AmigaOS 3.x.

## Current Status ✅

The multitasking library is **100% "WTF-Ready"** with:
- ✅ All core primitives implemented and tested
- ✅ Clean, focused API surface
- ✅ Static library build system
- ✅ Proper lifecycle management
- ✅ Race condition fixes in tests

## WebKit Integration Path

### 1. Freeze WebKit Snapshot

**Recommendation**: Pick an older, lean WebKit version
- **Target**: WebKit without JIT (LLInt only)
- **Disable**: Heavy features (WebAudio, WebGL, Media, IndexedDB, etc.)
- **Why**: Easier bring-up, fewer dependencies, faster compilation

**Options**:
- **OWB-era WebKit**: Lighter, more focused
- **Early WebKit2**: Before heavy features were added
- **WebKit1**: Single-process architecture (easier for initial port)

### 2. Add Amiga Platform to WTF

**Location**: `Source/WTF/wtf/Platform.h` or similar
```cpp
#if OS(AMIGA) || PLATFORM(AMIGA)
    #define WTF_PLATFORM_AMIGA 1
#endif
```

**Build System**: Update CMake/GNUmakefiles
```cmake
if(WTF_PLATFORM_AMIGA)
    add_definitions(-DOS_AMIGA)
    # Link our static library
    target_link_libraries(WTF libmultitask.a)
endif()
```

### 3. Implement WTF Threading Layer

**File**: `Source/WTF/wtf/threading/ThreadingAmiga.cpp` (or similar)
**Reference**: `ThreadingPOSIX.cpp` or `ThreadingWin.cpp` for exact class names

**Mapping**:
- `WTF::Thread` → `MyThread*`
- `WTF::Mutex` → `MyMutex`
- `WTF::Condition` → `MyCondition`
- `WTF::ThreadSpecific` → `MyTLS*`
- `WTF::MonotonicTime` → `CurrentTime()`/`now_ms()`
- `WTF::RunLoop` → `MyRunLoop` (use `MyRunLoopPoll` for single-threaded)

**Key Implementation Notes**:
- Use `MyRunLoopPoll` for single-threaded WebKit1 architecture
- Implement proper thread trampoline for WTF thread functions
- Handle TLS key management carefully
- Consider `AttemptSemaphore()` for non-blocking mutex try

### 4. Networking Stub

**Initial Approach**: Synchronous `bsdsocket.library`
```cpp
// Simple HTTP GET to fixed URL (proof of life)
// Later: async with completions posted to RunLoop
```

**Future**: Async networking with `MyRunLoopPost` for completions

### 5. Graphics Stub

**Initial**: RTG bitmap rendering and blitting
```cpp
// Basic bitmap operations
// Consider Cairo later for full painting parity
```

**Target**: WebKit1-style single-process rendering

### 6. Build Configuration

**Keep Static Library**: `libmultitask.a`
- Link into WTF/WebCore
- Fewer moving parts during bring-up
- Easier debugging and deployment

**Disable Heavy Features**:
```cpp
// In WebKit config
#define ENABLE_JIT 0
#define ENABLE_WEBGL 0
#define ENABLE_WEB_AUDIO 0
#define ENABLE_INDEXED_DATABASE 0
#define ENABLE_MEDIA_SOURCE 0
```

## Implementation Checklist

### Phase 1: Basic WTF Integration
- [ ] Add `OS(AMIGA)` to platform headers
- [ ] Drop in `ThreadingAmiga.cpp` skeleton
- [ ] Map basic threading primitives
- [ ] Test WTF compilation

### Phase 2: Core Functionality
- [ ] Implement thread creation/destruction
- [ ] Implement mutex/condition variables
- [ ] Implement TLS support
- [ ] Implement time functions
- [ ] Basic run loop integration

### Phase 3: WebKit Core
- [ ] Stub networking (bsdsocket.library)
- [ ] Stub graphics (RTG bitmap)
- [ ] Test basic HTML rendering
- [ ] Verify threading works correctly

### Phase 4: Polish
- [ ] Optimize critical sections
- [ ] Add proper error handling
- [ ] Performance tuning
- [ ] Documentation

## Key Technical Considerations

### 1. Forbid()/Permit() Usage
- **Current**: Used for 68k atomicity in `MyAtomic*` functions
- **Impact**: Global, pauses task switching
- **Guideline**: Keep critical sections tiny
- **Future**: Consider per-object semaphores if performance becomes issue

### 2. Memory Management
- **Consistent**: Use `AllocMem`/`FreeMem` throughout
- **TLS**: Fixed array approach (32 keys max)
- **Thread Cleanup**: Proper `MyThreadWait()` before `MyThreadDestroy()`

### 3. Run Loop Architecture
- **WebKit1**: Use `MyRunLoopPoll` for single-threaded
- **WebKit2**: Consider `MyRunLoopRun` in separate thread
- **Messages**: One-way posting via `MyRunLoopPost`

## Testing Strategy

### 1. Unit Tests
- [ ] Test each primitive individually
- [ ] Verify thread lifecycle
- [ ] Test race conditions
- [ ] Performance benchmarks

### 2. Integration Tests
- [ ] WTF threading layer
- [ ] Basic WebKit compilation
- [ ] Simple HTML rendering
- [ ] Network requests

### 3. Stress Tests
- [ ] Multiple concurrent threads
- [ ] Heavy mutex contention
- [ ] Memory pressure scenarios
- [ ] Long-running operations

## Next Steps

1. **Freeze WebKit snapshot** - Pick target version
2. **Set up build environment** - CMake/GNUmakefiles
3. **Implement WTF skeleton** - Use provided `ThreadingAmiga.cpp`
4. **Test basic compilation** - Verify WTF builds
5. **Add networking stub** - bsdsocket.library integration
6. **Add graphics stub** - RTG bitmap rendering
7. **Test HTML rendering** - Basic proof of concept

## Resources

- **Current Library**: `libmultitask.a` - Ready for integration
- **WTF Skeleton**: `ThreadingAmiga.cpp` - Starting point
- **Test Program**: `test_program.c` - Verification of primitives
- **Build System**: `Makefile` - Static library build

The foundation is solid. Time to start the WebKit adventure! 🚀
