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
  // sceKernelCreateEventFlag(name, attr, init, opt) -> id in v0
  auto createFlag = [](Asm& a, uint32_t init) { a.li(a0, 0); a.li(a1, 0); a.li(a2, init); a.li(a3, 0); a.sys("sceKernelCreateEventFlag"); };
  auto setFlag = [](Asm& a, uint32_t bits) { a.li(t2, DATA); a.lw(a0, t2, 0x10); a.li(a1, bits); a.sys("sceKernelSetEventFlag"); };
  auto waitFlag = [](Asm& a, uint32_t bits, uint32_t mode, uint32_t outPtr, uint32_t timeoutPtr) {
    a.li(t2, DATA); a.lw(a0, t2, 0x10); a.li(a1, bits); a.li(a2, mode); a.li(a3, outPtr); a.li(t0, timeoutPtr); a.sys("sceKernelWaitEventFlag"); };

  { Machine& m = *fresh(); Asm w(m, T1);  // OR wait with clear-matched-bits: reports the pattern at wake, then clears
    waitFlag(w, 0x3, 0x01 | 0x20, DATA + 0x20, 0); w.li(t2, DATA); w.li(t3, 7); w.sw(t3, t2, 0); w.ret(); w.install();
    Asm a(m, MAIN); createFlag(a, 0); a.li(t2, DATA); a.sw(v0, t2, 0x10); a.createStart(T1, 0x10, s0);
    a.li(t2, DATA); a.lw(t0, t2, 0); setFlag(a, 0x2); a.li(t2, DATA); a.lw(t1, t2, 0); a.brk(); a.install();
    CHECK(m.startMain(MAIN, 0x2000)); CHECK(m.run(100000).kind == RunResult::Break);
    CHECK(m.cpu.r[t0] == 0 && m.cpu.r[t1] == 7 && rd(m, DATA + 0x20) == 0x2 && m.flags[0].pattern == 0 && m.flags[0].waiters.empty()); delete &m; }
  { Machine& m = *fresh(); Asm w(m, T1);  // AND wait needs every bit, and does not clear without a clear option
    waitFlag(w, 0x5, 0x00, DATA + 0x20, 0); w.li(t2, DATA); w.li(t3, 7); w.sw(t3, t2, 0); w.ret(); w.install();
    Asm a(m, MAIN); createFlag(a, 0); a.li(t2, DATA); a.sw(v0, t2, 0x10); a.createStart(T1, 0x10, s0);
    setFlag(a, 0x1); a.li(t2, DATA); a.lw(t0, t2, 0); setFlag(a, 0x4); a.li(t2, DATA); a.lw(t1, t2, 0); a.brk(); a.install();
    CHECK(m.startMain(MAIN, 0x2000)); CHECK(m.run(100000).kind == RunResult::Break);
    CHECK(m.cpu.r[t0] == 0 && m.cpu.r[t1] == 7 && rd(m, DATA + 0x20) == 0x5 && m.flags[0].pattern == 0x5); delete &m; }
  { Machine& m = *fresh(); Asm w(m, T1);  // wait that already matches returns without blocking; clear-all wipes the pattern
    waitFlag(w, 0x1, 0x01 | 0x10, DATA + 0x20, 0); w.li(t2, DATA); w.sw(v0, t2, 0x40); w.ret(); w.install();
    Asm a(m, MAIN); createFlag(a, 0xF1); a.li(t2, DATA); a.sw(v0, t2, 0x10); a.createStart(T1, 0x10, s0); a.brk(); a.install();
    CHECK(m.startMain(MAIN, 0x2000)); CHECK(m.run(100000).kind == RunResult::Break);
    CHECK(rd(m, DATA + 0x40) == 0 && rd(m, DATA + 0x20) == 0xF1 && m.flags[0].pattern == 0); delete &m; }
  { Machine& m = *fresh(); Asm w(m, T1);  // timeout expires with an error and leaves the queue clean
    waitFlag(w, 0x1, 0x01, DATA + 0x20, DATA + 0x30); w.li(t2, DATA); w.sw(v0, t2, 0x40); w.ret(); w.install();
    Asm a(m, MAIN); createFlag(a, 0); a.li(t2, DATA); a.sw(v0, t2, 0x10); a.li(t3, 2000); a.sw(t3, t2, 0x30);
    a.createStart(T1, 0x10, s0); a.li(a0, 10000); a.sys("sceKernelDelayThread"); a.brk(); a.install();
    CHECK(m.startMain(MAIN, 0x2000)); CHECK(m.run(100000).kind == RunResult::Break);
    CHECK(rd(m, DATA + 0x40) == 0x80000001u && m.flags[0].waiters.empty()); delete &m; }
  { Machine& m = *fresh(); Asm a(m, MAIN);  // clear takes a keep-mask; poll and bad arguments
    createFlag(a, 0xFF); a.mov(s0, v0); a.mov(a0, s0); a.li(a1, 0x0F); a.sys("sceKernelClearEventFlag");
    a.mov(a0, s0); a.li(a1, 0xF0); a.li(a2, 0x01); a.li(a3, 0); a.sys("sceKernelPollEventFlag"); a.li(t2, DATA); a.sw(v0, t2, 0);       // no bit in 0xF0 is set
    a.mov(a0, s0); a.li(a1, 0x03); a.li(a2, 0x00); a.li(a3, 0); a.sys("sceKernelPollEventFlag"); a.li(t2, DATA); a.sw(v0, t2, 4);       // both bits set
    a.mov(a0, s0); a.li(a1, 0); a.li(a2, 0); a.li(a3, 0); a.sys("sceKernelPollEventFlag"); a.li(t2, DATA); a.sw(v0, t2, 8);            // empty mask is rejected
    a.li(a0, 999); a.li(a1, 1); a.sys("sceKernelSetEventFlag"); a.li(t2, DATA); a.sw(v0, t2, 12); a.brk(); a.install();
    CHECK(m.startMain(MAIN, 0x2000)); CHECK(m.run(100000).kind == RunResult::Break);
    CHECK(m.flags[0].pattern == 0x0F && rd(m, DATA) == 0x80000001u && rd(m, DATA + 4) == 0 && rd(m, DATA + 8) == 0x80000001u && rd(m, DATA + 12) == 0x80000001u); delete &m; }
  std::puts("ok event flags: OR/AND waits, clear modes, immediate match, timeout, poll, validation");

  // sceKernelCreateMutex(name, attr, initCount, opt) -> id in v0
  auto createMutex = [](Asm& a, uint32_t attr) { a.li(a0, 0); a.li(a1, attr); a.li(a2, 0); a.li(a3, 0); a.sys("sceKernelCreateMutex"); };
  auto mutexCall = [](Asm& a, const char* name, uint32_t n) { a.li(t2, DATA); a.lw(a0, t2, 0x10); a.li(a1, n); a.li(a2, 0); a.sys(name); };

  { Machine& m = *fresh(); Asm w(m, T1);  // a blocked locker gets the mutex when the owner unlocks
    mutexCall(w, "sceKernelLockMutex", 1); w.li(t2, DATA); w.li(t3, 7); w.sw(t3, t2, 0); w.ret(); w.install();
    Asm a(m, MAIN); createMutex(a, 0); a.li(t2, DATA); a.sw(v0, t2, 0x10); mutexCall(a, "sceKernelLockMutex", 1); a.createStart(T1, 0x10, s0);
    a.li(t2, DATA); a.lw(t0, t2, 0); mutexCall(a, "sceKernelUnlockMutex", 1); a.li(t2, DATA); a.lw(t1, t2, 0); a.brk(); a.install();
    CHECK(m.startMain(MAIN, 0x2000)); CHECK(m.run(100000).kind == RunResult::Break);
    CHECK(m.cpu.r[t0] == 0 && m.cpu.r[t1] == 7 && m.mutexes[0].owner == 2 && m.mutexes[0].count == 1 && m.mutexes[0].waiters.empty()); delete &m; }
  { Machine& m = *fresh(); Asm w(m, T1);  // try-lock fails for a non-owner; unlock by a non-owner fails
    mutexCall(w, "sceKernelTryLockMutex", 1); w.li(t2, DATA); w.sw(v0, t2, 0x40); mutexCall(w, "sceKernelUnlockMutex", 1); w.li(t2, DATA); w.sw(v0, t2, 0x44); w.ret(); w.install();
    Asm a(m, MAIN); createMutex(a, 0); a.li(t2, DATA); a.sw(v0, t2, 0x10); mutexCall(a, "sceKernelLockMutex", 1); a.createStart(T1, 0x10, s0); a.brk(); a.install();
    CHECK(m.startMain(MAIN, 0x2000)); CHECK(m.run(100000).kind == RunResult::Break);
    CHECK(rd(m, DATA + 0x40) == 0x80000001u && rd(m, DATA + 0x44) == 0x80000001u && m.mutexes[0].owner == 1); delete &m; }
  { Machine& m = *fresh(); Asm a(m, MAIN);  // plain mutex rejects a relock; recursive mutex counts
    createMutex(a, 0); a.li(t2, DATA); a.sw(v0, t2, 0x10); mutexCall(a, "sceKernelLockMutex", 1); mutexCall(a, "sceKernelLockMutex", 1); a.li(t2, DATA); a.sw(v0, t2, 0);
    createMutex(a, 0x200); a.li(t2, DATA); a.sw(v0, t2, 0x10); mutexCall(a, "sceKernelLockMutex", 1); mutexCall(a, "sceKernelLockMutex", 2); a.li(t2, DATA); a.sw(v0, t2, 4);
    mutexCall(a, "sceKernelUnlockMutex", 3); a.li(t2, DATA); a.sw(v0, t2, 8); mutexCall(a, "sceKernelUnlockMutex", 1); a.li(t2, DATA); a.sw(v0, t2, 12); a.brk(); a.install();
    CHECK(m.startMain(MAIN, 0x2000)); CHECK(m.run(100000).kind == RunResult::Break);
    CHECK(rd(m, DATA) == 0x80000001u && rd(m, DATA + 4) == 0 && rd(m, DATA + 8) == 0 && rd(m, DATA + 12) == 0x80000001u && m.mutexes[1].owner == 0); delete &m; }
  { Machine& m = *fresh(); Asm w(m, T1);  // lock wait times out; deleting a mutex releases waiters with an error
    w.li(t2, DATA); w.lw(a0, t2, 0x10); w.li(a1, 1); w.li(a2, DATA + 0x30); w.sys("sceKernelLockMutex"); w.li(t2, DATA); w.sw(v0, t2, 0x40); w.ret(); w.install();
    Asm a(m, MAIN); createMutex(a, 0); a.li(t2, DATA); a.sw(v0, t2, 0x10); mutexCall(a, "sceKernelLockMutex", 1); a.li(t3, 2000); a.sw(t3, t2, 0x30);
    a.createStart(T1, 0x10, s0); a.li(a0, 10000); a.sys("sceKernelDelayThread"); a.brk(); a.install();
    CHECK(m.startMain(MAIN, 0x2000)); CHECK(m.run(100000).kind == RunResult::Break);
    CHECK(rd(m, DATA + 0x40) == 0x80000001u && m.mutexes[0].waiters.empty() && m.mutexes[0].owner == 1); delete &m; }
  std::puts("ok mutexes: hand-off, try-lock, ownership, recursion, timeout");
  return 0;
}
