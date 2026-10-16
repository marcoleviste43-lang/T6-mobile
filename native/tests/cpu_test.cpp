#include "../cpu/mips.h"
#include "../loader/elf.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace t6;

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); std::exit(1); } } while (0)

enum { zero = 0, v0 = 2, t0 = 8, t1 = 9, t2 = 10, t3 = 11, t4 = 12, ra = 31 };
static uint32_t R(uint32_t rs, uint32_t rt, uint32_t rd, uint32_t sa, uint32_t fn, uint32_t op = 0) { return op << 26 | rs << 21 | rt << 16 | rd << 11 | sa << 6 | fn; }
static uint32_t I(uint32_t op, uint32_t rs, uint32_t rt, int32_t imm) { return op << 26 | rs << 21 | rt << 16 | ((uint32_t)imm & 0xFFFF); }
static const uint32_t NOP = 0, BREAK = 0x0d;
static const uint32_t BASE = 0x08804000;

struct Vm {
  Memory mem; Cpu cpu{&mem};
  Step go(const std::vector<uint32_t>& prog, uint64_t max = 100000) {
    for (size_t i = 0; i < prog.size(); i++) CHECK(mem.write32(BASE + 4 * (uint32_t)i, prog[i]));
    cpu.setPc(BASE); return cpu.run(max);
  }
};
static uint32_t ADDIU(int rt, int rs, int imm) { return I(9, rs, rt, imm); }

static void testLoopAndDelaySlots() {
  { Vm v;  // sum 1..10
    CHECK(v.go({ADDIU(t0, zero, 10), ADDIU(t1, zero, 0), R(t1, t0, t1, 0, 0x21), ADDIU(t0, t0, -1), I(5, t0, zero, -3), NOP, BREAK}) == Step::Break);
    CHECK(v.cpu.r[t1] == 55); }
  { Vm v;  // branch delay slot executes, target skips next
    CHECK(v.go({I(4, zero, zero, 2), ADDIU(t0, zero, 1), ADDIU(t1, zero, 2), BREAK}) == Step::Break);
    CHECK(v.cpu.r[t0] == 1 && v.cpu.r[t1] == 0); }
  { Vm v;  // beql not taken nullifies delay slot
    CHECK(v.go({ADDIU(t0, zero, 1), I(0x14, t0, zero, 2), ADDIU(t1, zero, 7), ADDIU(t2, zero, 9), BREAK}) == Step::Break);
    CHECK(v.cpu.r[t1] == 0 && v.cpu.r[t2] == 9); }
  { Vm v;  // bnel taken runs delay slot
    CHECK(v.go({ADDIU(t0, zero, 1), I(0x15, t0, zero, 2), ADDIU(t1, zero, 7), ADDIU(t2, zero, 9), BREAK}) == Step::Break);
    CHECK(v.cpu.r[t1] == 7 && v.cpu.r[t2] == 0); }
  { Vm v;  // jal / jr
    uint32_t tgt = ((BASE + 12) >> 2) & 0x3FFFFFF;
    CHECK(v.go({0x0c000000u | tgt, NOP, BREAK, ADDIU(v0, zero, 42), R(ra, 0, 0, 0, 0x08), NOP}) == Step::Break);
    CHECK(v.cpu.r[v0] == 42);
    CHECK(v.cpu.r[ra] == BASE + 8); }
  std::puts("ok loops, delay slots, branch-likely, jal/jr");
}

static void testMemory() {
  { Vm v;
    CHECK(v.go({I(0x0f, 0, t0, 0x0800), I(0x0d, t0, t0, 0x1000), ADDIU(t1, zero, -2), I(0x2b, t0, t1, 0), I(0x24, t0, t2, 0), I(0x20, t0, t3, 0), I(0x25, t0, t4, 2), BREAK}) == Step::Break);
    CHECK(v.cpu.r[t2] == 0xFE && v.cpu.r[t3] == 0xFFFFFFFEu && v.cpu.r[t4] == 0xFFFF); }
  { Vm v;  // unaligned load via lwr/lwl
    const uint8_t bytes[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    CHECK(v.mem.writeBytes(0x08001000, bytes, 8));
    CHECK(v.go({I(0x0f, 0, t0, 0x0800), I(0x0d, t0, t0, 0x1000), I(0x26, t0, t1, 1), I(0x22, t0, t1, 4), BREAK}) == Step::Break);
    CHECK(v.cpu.r[t1] == 0x55443322u); }
  { Vm v;  // unaligned store via swr/swl, then read back bytes
    CHECK(v.go({I(0x0f, 0, t0, 0x0800), I(0x0d, t0, t0, 0x1000), I(0x0f, 0, t1, 0xAABB), I(0x0d, t1, t1, 0xCCDD),
                I(0x2e, t0, t1, 1), I(0x2a, t0, t1, 4), BREAK}) == Step::Break);
    uint8_t b[8]; for (int i = 0; i < 8; i++) CHECK(v.mem.read8(0x08001000 + i, b[i]));
    CHECK(b[1] == 0xDD && b[2] == 0xCC && b[3] == 0xBB && b[4] == 0xAA && b[0] == 0 && b[5] == 0); }
  { Vm v;  CHECK(v.go({I(0x23, zero, t0, 0), BREAK}) == Step::MemFault); CHECK(v.cpu.faultAddr == 0 && v.cpu.pc == BASE); }
  { Vm v;  CHECK(v.go({I(0x0f, 0, t0, 0x0800), I(0x0d, t0, t0, 0x1001), I(0x23, t0, t1, 0), BREAK}) == Step::MemFault); }  // unaligned lw
  std::puts("ok loads/stores, unaligned lwl/lwr/swl/swr, faults");
}

static void testAlu() {
  { Vm v;  // mult/div
    CHECK(v.go({I(0x0f, 0, t0, 1), R(t0, t0, 0, 0, 0x19), R(0, 0, t1, 0, 0x10), R(0, 0, t2, 0, 0x12), BREAK}) == Step::Break);
    CHECK(v.cpu.r[t1] == 1 && v.cpu.r[t2] == 0); }
  { Vm v;
    CHECK(v.go({ADDIU(t0, zero, -7), ADDIU(t1, zero, 2), R(t0, t1, 0, 0, 0x1a), R(0, 0, t2, 0, 0x12), R(0, 0, t3, 0, 0x10), BREAK}) == Step::Break);
    CHECK((int32_t)v.cpu.r[t2] == -3 && (int32_t)v.cpu.r[t3] == -1); }
  { Vm v;
    CHECK(v.go({I(0x0f, 0, t0, 0xABCD), I(0x0d, t0, t0, 0x1234), R(t0, t1, 7, 8, 0, 0x1f), BREAK}) == Step::Break);
    CHECK(v.cpu.r[t1] == 0x12); }
  { Vm v;  // ins t2(rt),t3(rs) pos4 size8: rd=msb=11
    CHECK(v.go({ADDIU(t2, zero, -1), R(t3, t2, 11, 4, 4, 0x1f), BREAK}) == Step::Break);
    CHECK(v.cpu.r[t2] == 0xFFFFF00Fu); }
  { Vm v;  // seb rd=t1, rt=t0
    CHECK(v.go({ADDIU(t0, zero, 0x80), R(0, t0, t1, 0x10, 0x20, 0x1f), BREAK}) == Step::Break);
    CHECK(v.cpu.r[t1] == 0xFFFFFF80u); }
  { Vm v;  // rotr t1,t0,8
    CHECK(v.go({I(0x0f, 0, t0, 0x1234), I(0x0d, t0, t0, 0x5678), R(1, t0, t1, 8, 0x02), BREAK}) == Step::Break);
    CHECK(v.cpu.r[t1] == 0x78123456u); }
  { Vm v;  // clz
    CHECK(v.go({I(0x0f, 0, t0, 1), R(t0, 0, t1, 0, 0x16), BREAK}) == Step::Break); CHECK(v.cpu.r[t1] == 15); }
  { Vm v;  // max / movn / movz / sra / slt
    CHECK(v.go({ADDIU(t0, zero, -5), ADDIU(t1, zero, 3), R(t0, t1, t2, 0, 0x2c), ADDIU(t3, zero, 0), R(t1, t1, t3, 0, 0x0b),
                R(0, t0, t4, 1, 0x03), BREAK}) == Step::Break);
    CHECK(v.cpu.r[t2] == 3 && v.cpu.r[t3] == 3 && (int32_t)v.cpu.r[t4] == -3); }
  { Vm v;  // writes to $zero are discarded
    CHECK(v.go({ADDIU(zero, zero, 5), BREAK}) == Step::Break); CHECK(v.cpu.r[0] == 0); }
  std::puts("ok mult/div, ext/ins/seb/rotr/clz/max/movn/sra, $zero");
}

static void testSyscallAndBad() {
  { Vm v;  uint32_t sc = 0x2025u << 6 | 0x0c;
    CHECK(v.go({sc, BREAK}) == Step::Syscall); CHECK(v.cpu.syscallCode == 0x2025 && v.cpu.pc == BASE + 4);
    CHECK(v.cpu.run(10) == Step::Break); }
  { Vm v;  CHECK(v.go({0x44000000u /* COP1 */}) == Step::BadInstruction); CHECK(v.cpu.lastInstr == 0x44000000u); }
  { Vm v;  v.cpu.setPc(0x10); CHECK(v.cpu.step() == Step::MemFault); }  // fetch outside RAM
  { Vm v;  CHECK(v.go({0x08000000u | (BASE >> 2 & 0x3FFFFFF), NOP}, 50) == Step::Ok); }  // infinite loop hits budget
  std::puts("ok syscall/break, bad instruction, fetch fault, step budget");
}

static void put16(std::vector<uint8_t>& v, size_t o, uint16_t x) { v[o] = x; v[o + 1] = x >> 8; }
static void put32(std::vector<uint8_t>& v, size_t o, uint32_t x) { for (int i = 0; i < 4; i++) v[o + i] = x >> (8 * i); }
static std::vector<uint8_t> makeElf(uint32_t vaddr, uint32_t entry, const std::vector<uint32_t>& code, uint32_t memszExtra = 16) {
  std::vector<uint8_t> e(52 + 32 + code.size() * 4, 0);
  std::memcpy(e.data(), "\x7f" "ELF\1\1\1", 7);
  put16(e, 16, 2); put16(e, 18, 8); put32(e, 20, 1); put32(e, 24, entry); put32(e, 28, 52);
  put16(e, 40, 52); put16(e, 42, 32); put16(e, 44, 1);
  size_t ph = 52; put32(e, ph, 1); put32(e, ph + 4, 84); put32(e, ph + 8, vaddr); put32(e, ph + 12, vaddr);
  put32(e, ph + 16, (uint32_t)code.size() * 4); put32(e, ph + 20, (uint32_t)code.size() * 4 + memszExtra); put32(e, ph + 24, 5); put32(e, ph + 28, 4);
  for (size_t i = 0; i < code.size(); i++) put32(e, 84 + 4 * i, code[i]);
  return e;
}

static void testElf() {
  { Vm v; auto e = makeElf(BASE, BASE, {ADDIU(v0, zero, 7), BREAK}); ElfInfo info; std::string err;
    CHECK(loadElf(e.data(), e.size(), v.mem, info, &err)); CHECK(info.entry == BASE && info.segments == 1 && info.highestAddr == BASE + 8 + 16);
    v.cpu.setPc(info.entry); CHECK(v.cpu.run(100) == Step::Break); CHECK(v.cpu.r[v0] == 7); }
  { Vm v; ElfInfo i; std::string err; auto e = makeElf(BASE, BASE, {BREAK});
    auto bad = e; bad[0] = 'X'; CHECK(!loadElf(bad.data(), bad.size(), v.mem, i, &err));
    bad = e; put16(bad, 18, 3); CHECK(!loadElf(bad.data(), bad.size(), v.mem, i, &err) && err.find("MIPS") != std::string::npos);
    bad = e; put16(bad, 16, 0xFFA0); CHECK(!loadElf(bad.data(), bad.size(), v.mem, i, &err) && err.find("PRX") != std::string::npos);
    bad = makeElf(0x10000000, 0x10000000, {BREAK}); CHECK(!loadElf(bad.data(), bad.size(), v.mem, i, &err) && err.find("outside") != std::string::npos);
    bad = e; bad.resize(70); CHECK(!loadElf(bad.data(), bad.size(), v.mem, i, &err));
    bad = e; put32(bad, 52 + 16, 0x7fffffff); CHECK(!loadElf(bad.data(), bad.size(), v.mem, i, &err));
    bad = e; put32(bad, 24, 0x1000); CHECK(!loadElf(bad.data(), bad.size(), v.mem, i, &err)); }
  std::puts("ok ELF load + malformed ELF rejection");
}

int main() { testLoopAndDelaySlots(); testMemory(); testAlu(); testSyscallAndBad(); testElf(); std::puts("ALL CPU/ELF TESTS PASSED"); return 0; }
