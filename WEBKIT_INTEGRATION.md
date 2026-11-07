# WebKit Integration

This library provides the threading layer for
[OdysseyWebBrowser for AmigaOS 3.x (m68k)](https://github.com/themamboman/OdysseyWebBrowser-m68k),
a WebKit-based browser. This page describes how it is wired in, as a guide for similar ports.

## Status

OdysseyWebBrowser builds with this library and runs on AmigaOS 3.x (tested under WinUAE): pages
load, JavaScript runs, and the browser exits cleanly with several tabs open. See
[CHANGELOG.md](CHANGELOG.md) for the fixes this needed.

## How it is wired in

- **Compiled into WTF directly.** `Source/WTF/wtf/PlatformMUI.cmake` adds `multitask_lib.c` to
  WTF's sources and this directory to the include path. `libmultitask.a` is not used.
- **The adapter** is `Source/WTF/wtf/amiga/ThreadingAmiga.cpp` in the browser:

| WTF | This library |
|---|---|
| `Thread` create, join, detach | `MyThreadCreate`, `MyThreadWait` + `MyThreadDestroy`, `MyThreadDetach` |
| `Mutex` | `MyMutex*` (Exec `SignalSemaphore`) |
| `ThreadCondition` wait, timed wait, signal, broadcast | `MyConditionWait`, `MyConditionTimedWait`, `MyConditionSignal`, `MyConditionBroadcast` |
| `ThreadSpecific` | `MyTLSCreateWithDtor`, `MyTLSGet`, `MyTLSSet`, `MyTLSDestroy` |

- **Run loop:** the browser uses WTF's generic run loop, not `MyRunLoop`.
- **Networking:** the browser uses libcurl, not `amiga_networking.c`.
- **JavaScript:** JavaScriptCore runs with the C loop interpreter (`ENABLE_C_LOOP`, no JIT).

## Things a port must get right

- **Threads must be DOS processes.** WebKit threads call dos.library through the C library and
  libcurl, which is not allowed from a plain Exec task. `MyThreadCreate` uses `CreateNewProc`.
- **Stack size.** JavaScriptCore's call chains are deep; threads get 2 MB.
- **A real monotonic clock.** Timed waits rely on `amiga_now_ms()`, which reads timer.device's
  EClock. `ex_EClockFrequency` is ticks per second.
- **TLS for tasks the library did not create.** WTF also uses thread-local storage on the main
  task, so `MyTLSSet` keeps per-task storage for those.
- **Atomics use `Forbid()`/`Permit()`.** This stops all task switching, so keep those sections
  tiny.
