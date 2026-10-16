#include "../kernel/machine.h"
#include <cstdio>
#include <cstdlib>
using namespace t6;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); std::exit(1); } } while (0)

enum { v0 = 2, a0 = 4, a1 = 5, a2 = 6, a3 = 7, t0 = 8, t1 = 9, t2 = 10, t3 = 11, s0 = 16, s1 = 17 };
static const uint32_t MAIN = 0x08804000, T1 = 0x08805000, T2 = 0x08805100, DATA = 0x08001000;

struct Asm {
  Machine& m; uint32_t base; std::vector<uint32_t> w;
  Asm(Machine& mm, uint32_t b) : m(mm), base(b) {}
  void li(int rt, uint32_t v) { w.push_back(0x0fu << 26 | (uint32_t)rt << 16 | (v >> 16)); w.push_back(0x0du << 26 | (uint32_t)rt << 21 | (uint32_t)rt << 16 | (v & 0xFFFF)); }
  void sys(const char* n) { w.push_back(m.hle.bindName(n) << 6 | 0x0c); }
  void mov(int rd, int rs) { w.push_back((uint32_t)rs << 21 | (uint32_t)rd << 11 | 0x21); }
  void sw(int rt, int b, int off) { w.push_back(0x2bu << 26 | (uint32_t)b << 21 | (uint32_t)rt << 16 | (off & 0xFFFF)); }
  void lw(int rt, int b, int off) { w.push_back(0x23u << 26 | (uint32_t)b << 21 | (uint32_t)rt << 16 | (off & 0xFFFF)); }
  void ret() { w.push_back(31u << 21 | 8); w.push_back(0); }
  void brk() { w.push_back(0x0d); }
  void createStart(uint32_t entry, int prio, int saveReg, uint32_t argSize = 0, uint32_t argp = 0) {
    li(a0, 0); li(a1, entry); li(a2, (uint32_t)prio); li(a3, 0x1000); li(t0, 0); li(t1, 0); sys("sceKernelCreateThread");
    mov(saveReg, v0); mov(a0, saveReg); li(a1, argSize); li(a2, argp); sys("sceKernelStartThread");
  }
  void createSema(uint32_t init, uint32_t mx) { li(a0, 0); li(a1, 0); li(a2, init); li(a3, mx); li(t0, 0); sys("sceKernelCreateSema"); }
  void install() { for (size_t i = 0; i < w.size(); i++) CHECK(m.mem.write32(base + 4 * (uint32_t)i, w[i])); }
};
static uint32_t rd(Machine& m, uint32_t a) { uint32_t v = 0xDEADDEAD; m.mem.read32(a, v); return v; }
static Machine* fresh() { Machine* m = new Machine; m->setHeap(0x08900000, 0x0A000000); return m; }

int main() {
  { Machine& m = *fresh(); Asm th(m, T1);  // higher-priority thread preempts at start
    th.li(t2, DATA); th.li(t3, 1); th.sw(t3, t2, 0); th.ret(); th.install();
    Asm a(m, MAIN); a.createStart(T1, 0x10, s0); a.li(t2, DATA); a.lw(t0, t2, 0); a.brk(); a.install();
    CHECK(m.startMain(MAIN, 0x2000)); CHECK(m.run(100000).kind == RunResult::Break); CHECK(m.cpu.r[t0] == 1); delete &m; }
  { Machine& m = *fresh(); Asm th(m, T1);  // lower-priority thread waits until main blocks
    th.li(t2, DATA); th.li(t3, 1); th.sw(t3, t2, 0); th.ret(); th.install();
    Asm a(m, MAIN); a.createStart(T1, 0x30, s0); a.li(t2, DATA); a.lw(t0, t2, 0); a.li(a0, 1000); a.sys("sceKernelDelayThread"); a.lw(t1, t2, 0); a.brk(); a.install();
    CHECK(m.startMain(MAIN, 0x2000)); CHECK(m.run(100000).kind == RunResult::Break); CHECK(m.cpu.r[t0] == 0 && m.cpu.r[t1] == 1); delete &m; }
  std::puts("ok thread start, priority preemption, low-priority waits for a block");

  { Machine& m = *fresh(); Asm y(m, T1), x(m, T2);  // timed wake order: 1000us thread before 3000us thread
    y.li(a0, 1000); y.sys("sceKernelDelayThread"); y.li(t2, DATA); y.li(t3, 1); y.sw(t3, t2, 0); y.ret(); y.install();
    x.li(a0, 3000); x.sys("sceKernelDelayThread"); x.li(t2, DATA); x.lw(t3, t2, 0); x.sw(t3, t2, 4); x.ret(); x.install();
    Asm a(m, MAIN); a.createStart(T2, 0x20, s0); a.createStart(T1, 0x20, s1); a.li(a0, 10000); a.sys("sceKernelDelayThread"); a.brk(); a.install();
    CHECK(m.startMain(MAIN, 0x2000)); CHECK(m.run(200000).kind == RunResult::Break);
    CHECK(rd(m, DATA + 4) == 1 && m.nowUs() >= 10000); delete &m; }
  std::puts("ok timed delays wake in order, idle time skips forward");

  { Machine& m = *fresh(); Asm w(m, T1);  // semaphore hand-off
    w.li(t2, DATA); w.lw(a0, t2, 0x10); w.li(a1, 1); w.li(a2, 0); w.sys("sceKernelWaitSema");
    w.li(t2, DATA); w.li(t3, 7); w.sw(t3, t2, 0); w.ret(); w.install();
    Asm a(m, MAIN); a.createSema(0, 10); a.li(t2, DATA); a.sw(v0, t2, 0x10); a.createStart(T1, 0x10, s0);
    a.li(t2, DATA); a.lw(t0, t2, 0); a.lw(a0, t2, 0x10); a.li(a1, 1); a.sys("sceKernelSignalSema"); a.li(t2, DATA); a.lw(t1, t2, 0); a.brk(); a.install();
    CHECK(m.startMain(MAIN, 0x2000)); CHECK(m.run(100000).kind == RunResult::Break); CHECK(m.cpu.r[t0] == 0 && m.cpu.r[t1] == 7); delete &m; }
  { Machine& m = *fresh(); Asm w(m, T1);  // wait with timeout expires with an error
    w.li(t2, DATA); w.lw(a0, t2, 0x10); w.li(a1, 1); w.li(a2, DATA + 0x30); w.sys("sceKernelWaitSema");
    w.li(t2, DATA); w.sw(v0, t2, 0x40); w.ret(); w.install();
    Asm a(m, MAIN); a.createSema(0, 10); a.li(t2, DATA); a.sw(v0, t2, 0x10); a.li(t3, 2000); a.sw(t3, t2, 0x30);
    a.createStart(T1, 0x10, s0); a.li(a0, 10000); a.sys("sceKernelDelayThread"); a.brk(); a.install();
    CHECK(m.startMain(MAIN, 0x2000)); CHECK(m.run(100000).kind == RunResult::Break);
    CHECK(rd(m, DATA + 0x40) == 0x80000001u && m.semas[0].waiters.empty()); delete &m; }
  { Machine& m = *fresh(); Asm a(m, MAIN);  // poll semantics and bad ids
    a.createSema(1, 1); a.mov(s0, v0); a.mov(a0, s0); a.li(a1, 1); a.sys("sceKernelPollSema"); a.li(t2, DATA); a.sw(v0, t2, 0);
    a.mov(a0, s0); a.li(a1, 1); a.sys("sceKernelPollSema"); a.li(t2, DATA); a.sw(v0, t2, 4);
    a.mov(a0, s0); a.li(a1, 5); a.sys("sceKernelSignalSema"); a.li(t2, DATA); a.sw(v0, t2, 8);  // would exceed max
    a.li(a0, 999); a.li(a1, 0); a.li(a2, 0); a.sys("sceKernelStartThread"); a.li(t2, DATA); a.sw(v0, t2, 12); a.brk(); a.install();
    CHECK(m.startMain(MAIN, 0x2000)); CHECK(m.run(100000).kind == RunResult::Break);
    CHECK(rd(m, DATA) == 0 && rd(m, DATA + 4) == 0x80000001u && rd(m, DATA + 8) == 0x80000001u && rd(m, DATA + 12) == 0x80000001u); delete &m; }
  std::puts("ok semaphore hand-off, timeout, poll, argument validation");

  { Machine& m = *fresh(); Asm s(m, T1), s2(m, T2);  // sleep/wakeup, including a wakeup that arrives early
    s.sys("sceKernelSleepThread"); s.li(t2, DATA); s.li(t3, 5); s.sw(t3, t2, 0); s.ret(); s.install();
    s2.sys("sceKernelSleepThread"); s2.li(t2, DATA); s2.li(t3, 9); s2.sw(t3, t2, 4); s2.ret(); s2.install();
    Asm a(m, MAIN); a.createStart(T1, 0x10, s0); a.mov(a0, s0); a.sys("sceKernelWakeupThread");
    a.createStart(T2, 0x30, s1); a.mov(a0, s1); a.sys("sceKernelWakeupThread"); a.li(a0, 1000); a.sys("sceKernelDelayThread"); a.brk(); a.install();
    CHECK(m.startMain(MAIN, 0x2000)); CHECK(m.run(100000).kind == RunResult::Break); CHECK(rd(m, DATA) == 5 && rd(m, DATA + 4) == 9); delete &m; }
  std::puts("ok sleep/wakeup incl. early wakeup counted");

  { Machine& m = *fresh(); Asm th(m, T1);  // arguments are copied onto the new thread's stack
    th.li(t2, DATA); th.sw(a0, t2, 0x50); th.lw(t3, a1, 0); th.sw(t3, t2, 0x54); th.sw(a1, t2, 0x58); th.ret(); th.install();
    CHECK(m.mem.write32(DATA + 0x100, 0x11223344));
    Asm a(m, MAIN); a.createStart(T1, 0x10, s0, 8, DATA + 0x100); a.brk(); a.install();
    CHECK(m.startMain(MAIN, 0x2000)); CHECK(m.run(100000).kind == RunResult::Break);
    CHECK(rd(m, DATA + 0x50) == 8 && rd(m, DATA + 0x54) == 0x11223344u && rd(m, DATA + 0x58) != DATA + 0x100 && rd(m, DATA + 0x58) > 0x09F00000); delete &m; }
  std::puts("ok thread arguments copied to the stack");

  { Machine& m = *fresh(); Asm a(m, MAIN); a.createSema(0, 1); a.mov(a0, v0); a.li(a1, 1); a.li(a2, 0); a.sys("sceKernelWaitSema"); a.brk(); a.install();
    CHECK(m.startMain(MAIN, 0x2000)); CHECK(m.run(100000).kind == RunResult::Deadlock); delete &m; }
  { Machine& m = *fresh(); Asm a(m, MAIN); a.ret(); a.install();
    CHECK(m.startMain(MAIN, 0x2000)); CHECK(m.run(100000).kind == RunResult::AllThreadsExited); CHECK(m.threads[0].state == TState::Dead); delete &m; }
  std::puts("ok deadlock detection, return-from-main exits the thread");
  std::puts("ALL THREAD TESTS PASSED");
  return 0;
}
