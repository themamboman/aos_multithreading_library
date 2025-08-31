// ThreadingAmiga.cpp - WTF threading implementation for AmigaOS
// This file maps WebKit's threading primitives to our multitask library
// 
// Usage: Drop this into a WTF platform file (names vary by WebKit snapshot)
// Reference: ThreadingPOSIX.cpp or ThreadingWin.cpp for exact class names

#include "config.h"
#include "Threading.h"
#include "MonotonicTime.h"
#include "WallTime.h"
#include <memory>
#include <utility>

extern "C" {
#include "multitask_lib.h"
}

namespace WTF {

// ---------- Thread ----------
struct AmigaThread {
    MyThread* t { nullptr };
};

static void threadTrampoline(void* arg) {
    std::unique_ptr<std::pair<ThreadFunction, void*>> pair(
        static_cast<std::pair<ThreadFunction, void*>*>(arg));
    pair->first(pair->second);
}

Ref<Thread> Thread::create(const char* name, ThreadFunction entryPoint, void* data)
{
    // name is ignored in current MyThreadCreate, could add MyThreadCreateWithName
    auto* pair = new std::pair<ThreadFunction, void*>(entryPoint, data);
    MyThread* t = MyThreadCreate(threadTrampoline, pair);
    // In production, pass a small heap object that self-frees once the thread starts.
    return adoptRef(*new Thread(t));
}

Thread::~Thread()
{
    if (!m_handle)
        return;
    MyThreadWait(static_cast<MyThread*>(m_handle));
    MyThreadDestroy(static_cast<MyThread*>(m_handle));
}

bool Thread::waitForCompletion()
{
    if (!m_handle) return true;
    MyThreadWait(static_cast<MyThread*>(m_handle));
    return true;
}

// ---------- Mutex ----------
Mutex::Mutex() { MyMutexInit(&m_impl); }
Mutex::~Mutex() { MyMutexDestroy(&m_impl); }
void Mutex::lock() { MyMutexLock(&m_impl); }
bool Mutex::tryLock() { 
    extern "C" LONG AttemptSemaphore(struct SignalSemaphore*);
    return AttemptSemaphore(&m_impl.sem);
}
void Mutex::unlock() { MyMutexUnlock(&m_impl); }

// ---------- Condition ----------
Condition::Condition() { MyConditionInit(&m_impl); }
Condition::~Condition() { MyConditionDestroy(&m_impl); }
void Condition::wait(Mutex& m) { MyConditionWait(&m.m_impl, &m_impl); }
void Condition::notifyOne() { MyConditionSignal(&m_impl); }
void Condition::notifyAll() { MyConditionBroadcast(&m_impl); }

// ---------- TLS ----------
static MyTLSKey threadSpecificKey()
{
    static MyTLSKey key = 0;
    static bool inited = false;
    if (!inited) { key = MyTLSCreate(); inited = true; }
    return key;
}

void* threadSpecificGet() { return MyTLSGet(threadSpecificKey()); }
void  threadSpecificSet(void* v) { MyTLSSet(threadSpecificKey(), v); }

// ---------- Time ----------
MonotonicTime MonotonicTime::now()
{
    extern "C" ULONG amiga_now_ms(void);
    return MonotonicTime::fromMilliseconds(amiga_now_ms());
}

WallTime WallTime::now()
{
    // For AmigaOS 3.x, CurrentTime() gives seconds/micros since boot.
    // Convert to UNIX time only if your port expects real wall clock; otherwise use monotonic as wall.
    return WallTime::nowIgnoringLeapSeconds();
}

// ---------- RunLoop (very skeletal) ----------
class RunLoop::Impl {
public:
    MyRunLoop loop;
};

RunLoop::RunLoop()
    : m_impl(makeUnique<Impl>())
{
    MyRunLoopInit(&m_impl->loop);
}

RunLoop::~RunLoop()
{
    MyRunLoopDestroy(&m_impl->loop);
}

void RunLoop::run()
{
    MyRunLoopRun(&m_impl->loop);
}

void RunLoop::stop()
{
    MyRunLoopStop(&m_impl->loop);
}

void RunLoop::dispatch(Function<void()>&& fn)
{
    // allocate a heap thunk and post it
    struct Thunk { Function<void()> f; };
    auto* thunk = new Thunk{ WTFMove(fn) };
    MyRunLoopPost(&m_impl->loop, [](void* p){ auto* t = static_cast<Thunk*>(p); t->f(); delete t; }, thunk);
}

} // namespace WTF
