# WebKit Integration Steps

The steps used to bring this library into a WebKit port for AmigaOS 3.x. See
[WEBKIT_INTEGRATION.md](WEBKIT_INTEGRATION.md) for how the result is wired together.

1. **Build environment.** Cross-compile on Linux with the AmigaPorts `m68k-amigaos-gcc` and clib2,
   using a CMake toolchain file for the target.
2. **Add the library to WTF.** Compile `multitask_lib.c` as part of WTF and add this directory to
   the include paths of WTF, JavaScriptCore, WebCore and the browser.
3. **Write the threading adapter.** Implement WTF's `Thread`, `Mutex`, `ThreadCondition` and
   thread-specific storage on top of the `My*` functions (see the table in
   WEBKIT_INTEGRATION.md). Start from WebKit's POSIX threading file for the structure.
4. **Use generic WTF pieces where possible.** The generic run loop, work queue and main-thread
   implementations work; an Amiga file-system backend replaces the POSIX one.
5. **Turn off what 68k cannot use.** No JIT (C loop interpreter), no WebGL, no Web Audio, no video.
6. **Test on the target.** Run under an emulator such as WinUAE, and check thread start-up, timed
   waits, TLS on the main task and clean exit with threads running.

## Pitfalls found on the way

- A thread started as a plain Exec task must not call dos.library, and WebKit threads do.
- Thread stacks were raised from 256 KB to 2 MB for JavaScriptCore's deep call chains.
- `ex_EClockFrequency` is ticks per second; treating it as microseconds per tick made the clock
  run about 500,000 times too fast on PAL, so every timed wait expired at once.
- m68k GCC aligns pointers to only 2 bytes. Code that hides flags in the low bits of a static
  address (JavaScriptCore's `LazyProperty`) needs `alignas(4)`.
