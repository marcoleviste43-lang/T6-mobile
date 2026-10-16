// Thread and semaphore HLE services. Error codes are placeholders (0x80000001), not real PSP values.
#include "machine.h"

namespace t6 {
namespace {
constexpr uint32_t kErr = 0x80000001u;
uint32_t arg(Machine& m, int i) { return i < 4 ? m.cpu.r[4 + i] : m.cpu.r[8 + (i - 4)]; }  // a0-a3, then t0-t3
void ret(Machine& m, uint32_t v) { m.cpu.r[2] = v; }
Sema* sema(Machine& m, uint32_t id) { return (id >= 1 && id <= m.semas.size() && m.semas[id - 1].live) ? &m.semas[id - 1] : nullptr; }

// Wakes waiters in strict FIFO order while the head waiter's request can be satisfied.
void serveWaiters(Machine& m, Sema& s) {
  while (!s.waiters.empty()) {
    Thread& t = m.threads[s.waiters.front() - 1];
    if (s.count < (int32_t)t.semaNeed) break;
    s.count -= (int32_t)t.semaNeed;
    s.waiters.erase(s.waiters.begin());
    t.timed = false;
    m.wakeWithResult(t, 0);
  }
}
}  // namespace

void registerThreadServices(Hle& hle) {
  hle.add("sceKernelCreateThread", [](Machine& m) {  // (name, entry, priority, stackSize, attr, option)
    uint32_t id = m.createThread("thread", arg(m, 1), (int)arg(m, 2), arg(m, 3));
    ret(m, id ? id : kErr);
  });
  hle.add("sceKernelStartThread", [](Machine& m) { ret(m, m.startThread(arg(m, 0), arg(m, 1), arg(m, 2)) ? 0 : kErr); });
  hle.add("sceKernelGetThreadId", [](Machine& m) { ret(m, m.curId); });
  hle.add("sceKernelExitThread", [](Machine& m) {
    if (!m.threaded()) { m.exited = true; return; }
    Thread& t = m.cur(); t.state = TState::Dead; t.exitStatus = arg(m, 0);
  });
  hle.add("sceKernelExitDeleteThread", [](Machine& m) {
    if (!m.threaded()) { m.exited = true; return; }
    Thread& t = m.cur(); t.state = TState::Dead; t.exitStatus = arg(m, 0); t.deleted = true;
  });
  hle.add("sceKernelDeleteThread", [](Machine& m) {
    Thread* t = m.thread(arg(m, 0));
    if (!t || (t->state != TState::Dormant && t->state != TState::Dead)) { ret(m, kErr); return; }
    t->deleted = true; ret(m, 0);
  });
  hle.add("sceKernelSleepThread", [](Machine& m) {
    if (!m.threaded()) { ret(m, kErr); return; }
    Thread& t = m.cur();
    if (t.wakeups > 0) { t.wakeups--; ret(m, 0); return; }
    t.state = TState::Sleeping; t.timed = false;
  });
  hle.add("sceKernelWakeupThread", [](Machine& m) {
    Thread* t = m.thread(arg(m, 0));
    if (!t || t->state == TState::Dead || t->state == TState::Dormant) { ret(m, kErr); return; }
    if (t->state == TState::Sleeping) m.wakeWithResult(*t, 0); else t->wakeups++;
    ret(m, 0);
  });

  hle.add("sceKernelCreateSema", [](Machine& m) {  // (name, attr, initCount, maxCount, option)
    int32_t init = (int32_t)arg(m, 2), mx = (int32_t)arg(m, 3);
    if (mx <= 0 || init < 0 || init > mx) { ret(m, kErr); return; }
    Sema s; s.count = init; s.maxCount = mx; m.semas.push_back(s);
    ret(m, (uint32_t)m.semas.size());
  });
  hle.add("sceKernelDeleteSema", [](Machine& m) {
    Sema* s = sema(m, arg(m, 0));
    if (!s) { ret(m, kErr); return; }
    for (uint32_t w : s->waiters) m.wakeWithResult(m.threads[w - 1], kErr);  // waiters are released with an error
    s->waiters.clear(); s->live = false; ret(m, 0);
  });
  hle.add("sceKernelSignalSema", [](Machine& m) {
    Sema* s = sema(m, arg(m, 0)); int32_t n = (int32_t)arg(m, 1);
    if (!s || n <= 0 || s->count + n > s->maxCount) { ret(m, kErr); return; }
    s->count += n; serveWaiters(m, *s); ret(m, 0);
  });
  hle.add("sceKernelPollSema", [](Machine& m) {
    Sema* s = sema(m, arg(m, 0)); int32_t need = (int32_t)arg(m, 1);
    if (!s || need <= 0 || s->count < need || !s->waiters.empty()) { ret(m, kErr); return; }
    s->count -= need; ret(m, 0);
  });
  hle.add("sceKernelWaitSema", [](Machine& m) {  // (id, need, timeoutPtr)
    Sema* s = sema(m, arg(m, 0)); int32_t need = (int32_t)arg(m, 1); uint32_t tp = arg(m, 2);
    if (!m.threaded() || !s || need <= 0 || need > s->maxCount) { ret(m, kErr); return; }
    if (s->count >= need && s->waiters.empty()) { s->count -= need; ret(m, 0); return; }
    Thread& t = m.cur();
    t.state = TState::WaitSema; t.semaId = arg(m, 0); t.semaNeed = (uint32_t)need; t.timed = false;
    if (tp) { uint32_t us; if (m.mem.read32(tp, us)) { t.timed = true; t.wakeAt = m.nowUs() + us; } }
    s->waiters.push_back(t.id);
  });
}

}  // namespace t6
