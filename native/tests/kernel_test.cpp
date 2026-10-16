#include "../kernel/machine.h"
#include <cstdio>
#include <cstdlib>
using namespace t6;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); std::exit(1); } } while (0)

static const uint32_t BASE = 0x08804000;
static uint32_t LUI(int rt, uint32_t v) { return 0x0fu << 26 | (uint32_t)rt << 16 | (v & 0xFFFF); }
static uint32_t ORI(int rt, int rs, uint32_t v) { return 0x0du << 26 | (uint32_t)rs << 21 | (uint32_t)rt << 16 | (v & 0xFFFF); }
static const uint32_t BREAK = 0x0d;

// Loads up to 4 args into a0..a3, issues the named syscall, then BREAK. Returns the run result.
static RunResult call(Machine& m, const char* name, std::initializer_list<uint32_t> args, uint32_t* v0 = nullptr) {
  std::vector<uint32_t> p; int i = 4;
  for (uint32_t a : args) { p.push_back(LUI(i, a >> 16)); p.push_back(ORI(i, i, a & 0xFFFF)); i++; }
  p.push_back(m.hle.bindName(name) << 6 | 0x0c); p.push_back(BREAK);
  for (size_t k = 0; k < p.size(); k++) CHECK(m.mem.write32(BASE + 4 * (uint32_t)k, p[k]));
  m.cpu.setPc(BASE);
  RunResult r = m.run(1000);
  if (v0) *v0 = m.cpu.r[2];
  return r;
}

int main() {
  { uint8_t d[20]; sha1((const uint8_t*)"abc", 3, d);
    const uint8_t want[20] = {0xa9,0x99,0x3e,0x36,0x47,0x06,0x81,0x6a,0xba,0x3e,0x25,0x71,0x78,0x50,0xc2,0x6c,0x9c,0xd0,0xd8,0x9d};
    CHECK(std::memcmp(d, want, 20) == 0); }
  CHECK(nidOf("sceKernelExitGame") == 0x05572A5Fu);
  CHECK(nidOf("sceDisplaySetMode") == 0x0E20F177u);
  CHECK(nidOf("sceCtrlReadBufferPositive") == 0x1F803938u);
  CHECK(nidOf("sceKernelDelayThread") == 0xCEADEB47u);
  std::puts("ok SHA-1 vector and NID derivation");

  { Machine m; m.mem.write32(BASE, m.hle.bindName("sceKernelExitGame") << 6 | 0x0c); m.cpu.setPc(BASE);
    CHECK(m.run(100).kind == RunResult::Exited); }
  std::puts("ok sceKernelExitGame stops the machine");

  { Machine m; m.buttons = 0x4000 | 0x0008; m.lx = 10; m.ly = 255; uint32_t v0;
    CHECK(call(m, "sceCtrlReadBufferPositive", {0x08001000, 2}, &v0).kind == RunResult::Break && v0 == 2);
    uint32_t b; uint8_t lx, ly; CHECK(m.mem.read32(0x08001004, b) && b == 0x4008);
    CHECK(m.mem.read8(0x08001008, lx) && lx == 10 && m.mem.read8(0x08001009, ly) && ly == 255);
    CHECK(m.mem.read32(0x08001014, b) && b == 0x4008);  // second entry
    call(m, "sceCtrlReadBufferPositive", {0, 1}, &v0); CHECK(v0 == 0x80000001u);          // bad pointer
    call(m, "sceCtrlReadBufferPositive", {0x08001000, 0}, &v0); CHECK(v0 == 0x80000001u);   // bad count
    call(m, "sceCtrlReadBufferPositive", {0x08001000, 1000}, &v0); CHECK(v0 == 0x80000001u); }
  std::puts("ok controller read, bad arguments rejected");

  { Machine m; uint32_t t0, t1;
    call(m, "sceKernelGetSystemTimeLow", {}, &t0); call(m, "sceKernelDelayThread", {5000});
    call(m, "sceKernelGetSystemTimeLow", {}, &t1); CHECK(t1 - t0 >= 5000);
    uint32_t lo; call(m, "sceKernelGetSystemTimeWide", {}, &lo); CHECK(m.cpu.r[3] == 0);
    uint64_t before = m.nowUs(); call(m, "sceDisplayWaitVblankStart", {});
    CHECK(m.vblanks == 1 && m.nowUs() > before && m.nowUs() % 16683 < 100);  // landed just after a vblank boundary
    call(m, "sceDisplaySetMode", {0, 480, 272}, &lo); CHECK(lo == 0 && m.dispW == 480 && m.dispH == 272);
    call(m, "sceDisplaySetMode", {0, 0, 272}, &lo); CHECK(lo == 0x80000001u); }
  std::puts("ok virtual time, delay, vblank, display mode");

  { Machine m; m.setHeap(0x08900000, 0x0A000000); uint32_t a, b, c, addr;
    call(m, "sceKernelAllocPartitionMemory", {2, 0, 0, 0x1000, 0}, &a);
    call(m, "sceKernelAllocPartitionMemory", {2, 0, 0, 0x1001, 0}, &b);
    call(m, "sceKernelAllocPartitionMemory", {2, 0, 1, 0x2000, 0}, &c);
    CHECK(a == 1 && b == 2 && c == 3);
    call(m, "sceKernelGetBlockHeadAddr", {a}, &addr); CHECK(addr == 0x08900100);
    call(m, "sceKernelGetBlockHeadAddr", {b}, &addr); CHECK(addr == 0x08901100);
    call(m, "sceKernelGetBlockHeadAddr", {c}, &addr); CHECK(addr == 0x0A000000 - 0x2000);
    call(m, "sceKernelFreePartitionMemory", {a}, &addr); CHECK(addr == 0);
    call(m, "sceKernelGetBlockHeadAddr", {a}, &addr); CHECK(addr == 0);
    call(m, "sceKernelFreePartitionMemory", {a}, &addr); CHECK(addr == 0x80000001u);
    call(m, "sceKernelAllocPartitionMemory", {2, 0, 0, 0x7000000, 0}, &addr); CHECK(addr == 0x80000001u); }
  std::puts("ok partition memory allocate/lookup/free/exhaustion");

  { Machine m; uint32_t code = m.hle.bindNid(0xDEADBEEF);
    m.mem.write32(BASE, code << 6 | 0x0c); m.cpu.setPc(BASE);
    RunResult r = m.run(100); CHECK(r.kind == RunResult::UnimplementedSyscall && r.nid == 0xDEADBEEFu);
    CHECK(m.hle.bindNid(0xDEADBEEF) == code); }
  std::puts("ok unknown imports report their NID instead of silently succeeding");
  std::puts("ALL KERNEL TESTS PASSED");
  return 0;
}
