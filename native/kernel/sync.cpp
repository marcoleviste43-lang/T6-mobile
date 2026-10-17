// Event flag and mutex HLE services. Written from public SDK/emulator documentation and NOT yet
// checked against real game behavior. Error codes are placeholders (0x80000001), not real PSP values.
#include "machine.h"

namespace t6 {
namespace {
constexpr uint32_t kErr = 0x80000001u;
constexpr uint32_t kWaitOr = 0x01, kClearAll = 0x10, kClearBits = 0x20;  // event flag wait mode bits
constexpr uint32_t kMutexRecursive = 0x200;                              // mutex attribute bit

uint32_t arg(Machine& m, int i) { return i < 4 ? m.cpu.r[4 + i] : m.cpu.r[8 + (i - 4)]; }  // a0-a3, then t0-t3
void ret(Machine& m, uint32_t v) { m.cpu.r[2] = v; }
EventFlag* flag(Machine& m, uint32_t id) { return (id >= 1 && id <= m.flags.size() && m.flags[id - 1].live) ? &m.flags[id - 1] : nullptr; }
Mutex* mutex(Machine& m, uint32_t id) { return (id >= 1 && id <= m.mutexes.size() && m.mutexes[id - 1].live) ? &m.mutexes[id - 1] : nullptr; }

bool matches(uint32_t pattern, uint32_t bits, uint32_t mode) {
  return (mode & kWaitOr) ? (pattern & bits) != 0 : (pattern & bits) == bits;
}
// Reports the pattern seen at wake time, then applies the clear options.
void consume(Machine& m, EventFlag& f, uint32_t bits, uint32_t mode, uint32_t outPtr) {
  if (outPtr) m.mem.write32(outPtr, f.pattern);
  if (mode & kClearAll) f.pattern = 0;
  if (mode & kClearBits) f.pattern &= ~bits;
}
// Waiters are checked in queue order; each wake can clear bits that later waiters needed.
void serveFlagWaiters(Machine& m, EventFlag& f) {
  for (size_t i = 0; i < f.waiters.size();) {
    Thread& t = m.threads[f.waiters[i] - 1];
    if (!matches(f.pattern, t.evBits, t.evMode)) { i++; continue; }
    consume(m, f, t.evBits, t.evMode, t.evOutPtr);
    f.waiters.erase(f.waiters.begin() + (long)i);
    t.timed = false; m.wakeWithResult(t, 0);
  }
}
// Gives the mutex to the first waiter, if any.
void handOff(Machine& m, Mutex& mx) {
  if (mx.waiters.empty()) return;
  Thread& t = m.threads[mx.waiters.front() - 1];
  mx.waiters.erase(mx.waiters.begin());
  mx.owner = t.id; mx.count = t.mutexNeed;
  t.timed = false; m.wakeWithResult(t, 0);
}
void block(Machine& m, Thread& t, TState st, uint32_t timeoutPtr) {
  t.state = st; t.timed = false;
  if (timeoutPtr) { uint32_t us; if (m.mem.read32(timeoutPtr, us)) { t.timed = true; t.wakeAt = m.nowUs() + us; } }
}
}  // namespace

void registerSyncServices(Hle& hle) {
  hle.add("sceKernelCreateEventFlag", [](Machine& m) {  // (name, attr, initPattern, option)
    EventFlag f; f.pattern = arg(m, 2); m.flags.push_back(f);
    ret(m, (uint32_t)m.flags.size());
  });
  hle.add("sceKernelDeleteEventFlag", [](Machine& m) {
    EventFlag* f = flag(m, arg(m, 0));
    if (!f) { ret(m, kErr); return; }
    for (uint32_t w : f->waiters) m.wakeWithResult(m.threads[w - 1], kErr);
    f->waiters.clear(); f->live = false; ret(m, 0);
  });
  hle.add("sceKernelSetEventFlag", [](Machine& m) {  // (id, bits): pattern |= bits
    EventFlag* f = flag(m, arg(m, 0));
    if (!f) { ret(m, kErr); return; }
    f->pattern |= arg(m, 1); serveFlagWaiters(m, *f); ret(m, 0);
  });
  hle.add("sceKernelClearEventFlag", [](Machine& m) {  // (id, mask): pattern &= mask (the argument is a keep-mask)
    EventFlag* f = flag(m, arg(m, 0));
    if (!f) { ret(m, kErr); return; }
    f->pattern &= arg(m, 1); ret(m, 0);
  });
  hle.add("sceKernelPollEventFlag", [](Machine& m) {  // (id, bits, mode, outPtr)
    EventFlag* f = flag(m, arg(m, 0)); uint32_t bits = arg(m, 1), mode = arg(m, 2);
    if (!f || bits == 0 || !f->waiters.empty() || !matches(f->pattern, bits, mode)) { ret(m, kErr); return; }
    consume(m, *f, bits, mode, arg(m, 3)); ret(m, 0);
  });
  hle.add("sceKernelWaitEventFlag", [](Machine& m) {  // (id, bits, mode, outPtr, timeoutPtr)
    EventFlag* f = flag(m, arg(m, 0)); uint32_t bits = arg(m, 1), mode = arg(m, 2), out = arg(m, 3), tp = arg(m, 4);
    if (!m.threaded() || !f || bits == 0) { ret(m, kErr); return; }
    if (f->waiters.empty() && matches(f->pattern, bits, mode)) { consume(m, *f, bits, mode, out); ret(m, 0); return; }
    Thread& t = m.cur();
    t.evId = arg(m, 0); t.evBits = bits; t.evMode = mode; t.evOutPtr = out;
    block(m, t, TState::WaitEvent, tp);
    f->waiters.push_back(t.id);
  });

  hle.add("sceKernelCreateMutex", [](Machine& m) {  // (name, attr, initCount, option)
    uint32_t attr = arg(m, 1), init = arg(m, 2);
    if (init > 1 && !(attr & kMutexRecursive)) { ret(m, kErr); return; }
    Mutex mx; mx.attr = attr;
    if (init > 0 && m.threaded()) { mx.owner = m.curId; mx.count = init; }
    m.mutexes.push_back(mx);
    ret(m, (uint32_t)m.mutexes.size());
  });
  hle.add("sceKernelDeleteMutex", [](Machine& m) {
    Mutex* mx = mutex(m, arg(m, 0));
    if (!mx) { ret(m, kErr); return; }
    for (uint32_t w : mx->waiters) m.wakeWithResult(m.threads[w - 1], kErr);
    mx->waiters.clear(); mx->live = false; ret(m, 0);
  });
  auto lock = [](Machine& m, bool wait) {  // (id, count, timeoutPtr)
    Mutex* mx = mutex(m, arg(m, 0)); uint32_t n = arg(m, 1);
    if (!m.threaded() || !mx || n == 0) { ret(m, kErr); return; }
    bool recursive = mx->attr & kMutexRecursive;
    if (mx->owner == 0) {
      if (n > 1 && !recursive) { ret(m, kErr); return; }
      mx->owner = m.curId; mx->count = n; ret(m, 0); return;
    }
    if (mx->owner == m.curId) {
      if (!recursive) { ret(m, kErr); return; }  // a plain mutex cannot be locked twice by its owner
      mx->count += n; ret(m, 0); return;
    }
    if (!wait) { ret(m, kErr); return; }
    Thread& t = m.cur();
    t.mutexId = arg(m, 0); t.mutexNeed = n;
    block(m, t, TState::WaitMutex, arg(m, 2));
    mx->waiters.push_back(t.id);
  };
  hle.add("sceKernelLockMutex", [lock](Machine& m) { lock(m, true); });
  hle.add("sceKernelTryLockMutex", [lock](Machine& m) { lock(m, false); });
  hle.add("sceKernelUnlockMutex", [](Machine& m) {  // (id, count)
    Mutex* mx = mutex(m, arg(m, 0)); uint32_t n = arg(m, 1);
    if (!m.threaded() || !mx || n == 0 || mx->owner != m.curId || mx->count < n) { ret(m, kErr); return; }
    mx->count -= n;
    if (mx->count == 0) { mx->owner = 0; handOff(m, *mx); }
    ret(m, 0);
  });
}

}  // namespace t6
