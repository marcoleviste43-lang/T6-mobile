// VFPU tests. The encoders below follow the same from-memory encoding the interpreter uses, so these tests prove the
// semantics (math, register views, prefixes, rejection of unsupported forms) are internally consistent, NOT that the
// encodings match real hardware. See the header comment of cpu/vfpu.cpp.
#include "../cpu/mips.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace t6;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); std::exit(1); } } while (0)

static const uint32_t BASE = 0x08804000, DATA = 0x08001000, BREAK = 0x0d;
enum { t0 = 8, t1 = 9 };

static uint32_t szbits(int n) { return (uint32_t)(((n - 1) & 1) << 7 | (((n - 1) >> 1) & 1) << 15); }
static uint32_t enc3(uint32_t op, uint32_t fn, int n, uint32_t vd, uint32_t vs, uint32_t vt) { return op << 26 | fn << 23 | vt << 16 | vs << 8 | vd | szbits(n); }
static uint32_t vadd(int n, uint32_t d, uint32_t s, uint32_t t) { return enc3(0x18, 0, n, d, s, t); }
static uint32_t vsub(int n, uint32_t d, uint32_t s, uint32_t t) { return enc3(0x18, 1, n, d, s, t); }
static uint32_t vdiv(int n, uint32_t d, uint32_t s, uint32_t t) { return enc3(0x18, 7, n, d, s, t); }
static uint32_t vmul(int n, uint32_t d, uint32_t s, uint32_t t) { return enc3(0x19, 0, n, d, s, t); }
static uint32_t vdot(int n, uint32_t d, uint32_t s, uint32_t t) { return enc3(0x19, 1, n, d, s, t); }
static uint32_t vscl(int n, uint32_t d, uint32_t s, uint32_t t) { return enc3(0x19, 2, n, d, s, t); }
static uint32_t vhdp(int n, uint32_t d, uint32_t s, uint32_t t) { return enc3(0x19, 4, n, d, s, t); }
static uint32_t vun(uint32_t fn, int n, uint32_t d, uint32_t s) { return 0x34u << 26 | fn << 16 | s << 8 | d | szbits(n); }
enum { VMOV = 0, VABS = 1, VNEG = 2, VSAT0 = 4, VSAT1 = 5, VZERO = 6, VONE = 7, VRCP = 16, VRSQ = 17, VSIN = 18, VCOS = 19, VSQRT = 22 };
static uint32_t pfx(uint32_t kind, uint32_t imm) { return 0x37u << 26 | kind << 24 | imm; }
static uint32_t lvq(uint32_t vt, uint32_t base, int off) { return 0x36u << 26 | base << 21 | (vt & 31) << 16 | ((uint32_t)off & 0xFFFC) | ((vt >> 5) & 1); }
static uint32_t svq(uint32_t vt, uint32_t base, int off) { return 0x3Eu << 26 | base << 21 | (vt & 31) << 16 | ((uint32_t)off & 0xFFFC) | ((vt >> 5) & 1); }
static uint32_t lvs(uint32_t vt, uint32_t base, int off) { return 0x32u << 26 | base << 21 | (vt & 31) << 16 | ((uint32_t)off & 0xFFFC) | ((vt >> 5) & 3); }
static uint32_t svs(uint32_t vt, uint32_t base, int off) { return 0x3Au << 26 | base << 21 | (vt & 31) << 16 | ((uint32_t)off & 0xFFFC) | ((vt >> 5) & 3); }
static uint32_t mtv(uint32_t rt, uint32_t v) { return 0x48E00000u | rt << 16 | v; }
static uint32_t mfv(uint32_t rt, uint32_t v) { return 0x48600000u | rt << 16 | v; }
static uint32_t viim(uint32_t vt, int imm) { return 0xDFu << 24 | vt << 16 | ((uint32_t)imm & 0xFFFF); }
// 7-bit register names: column vector, row vector, single
static uint32_t COL(uint32_t m, uint32_t c) { return m << 2 | c; }
static uint32_t ROW(uint32_t m, uint32_t r) { return 0x20 | m << 2 | r; }
static uint32_t SGL(uint32_t m, uint32_t c, uint32_t r) { return r << 5 | m << 2 | c; }
static uint32_t LUI(int rt, uint32_t v) { return 0x0Fu << 26 | (uint32_t)rt << 16 | (v & 0xFFFF); }
static uint32_t ORI(int rt, int rs, uint32_t v) { return 0x0Du << 26 | (uint32_t)rs << 21 | (uint32_t)rt << 16 | (v & 0xFFFF); }
static std::vector<uint32_t> setBase(int reg, uint32_t addr) { return {LUI(reg, addr >> 16), ORI(reg, reg, addr & 0xFFFF)}; }

static uint32_t bits(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }
static float flt(uint32_t u) { float f; std::memcpy(&f, &u, 4); return f; }

struct Vm {
  Memory mem; Cpu cpu{&mem};
  void put(uint32_t off, std::initializer_list<float> v) { uint32_t a = DATA + off; for (float f : v) { CHECK(mem.write32(a, bits(f))); a += 4; } }
  float get(uint32_t off) { uint32_t v = 0; CHECK(mem.read32(DATA + off, v)); return flt(v); }
  Step go(std::vector<uint32_t> prog) {  // t0 = DATA, then the program, then break
    std::vector<uint32_t> all = setBase(t0, DATA); all.insert(all.end(), prog.begin(), prog.end()); all.push_back(BREAK);
    for (size_t i = 0; i < all.size(); i++) CHECK(mem.write32(BASE + 4 * (uint32_t)i, all[i]));
    cpu.setPc(BASE); return cpu.run(100000);
  }
  bool ok(std::vector<uint32_t> prog) { return go(prog) == Step::Break; }
};
static bool near(float a, float b, float eps = 1e-6f) { return std::fabs(a - b) <= eps; }

int main() {
  { Vm v; v.put(0, {1, 2, 3, 4});  // register-file layout: column c lane i = M[i][c]; row r lane i = M[r][i]
    CHECK(v.ok({lvq(COL(0, 1), t0, 0), svq(COL(0, 1), t0, 16), svq(ROW(0, 0), t0, 32), svq(ROW(0, 2), t0, 48)}));
    CHECK(v.get(16) == 1 && v.get(20) == 2 && v.get(24) == 3 && v.get(28) == 4);  // column round trip
    CHECK(v.get(32) == 0 && v.get(36) == 1 && v.get(40) == 0 && v.get(44) == 0);  // row 0 sees M[0][1] = 1 in lane 1
    CHECK(v.get(48) == 0 && v.get(52) == 3 && v.get(56) == 0);                    // row 2 sees M[2][1] = 3
    CHECK(v.cpu.vr[0 * 4 + 1 + 0 * 32] == 1 && v.cpu.vr[1 + 32] == 2 && v.cpu.vr[1 + 64] == 3 && v.cpu.vr[1 + 96] == 4); }
  { Vm v; v.put(0, {1, 2, 3, 4});  // misaligned quad access faults and leaves pc on the instruction
    CHECK(v.go({lvq(COL(0, 0), t0, 4)}) == Step::MemFault); CHECK(v.cpu.faultAddr == DATA + 4);
    Vm w; CHECK(w.go({0x3Eu << 26 | t0 << 21 | 2u}) == Step::BadInstruction); }  // sv.q writeback form is rejected
  { Vm v; v.put(0, {1, 2, 3, 4}); v.put(16, {10, 20, 30, 40});  // elementwise ops at each size
    CHECK(v.ok({lvq(COL(0, 0), t0, 0), lvq(COL(0, 1), t0, 16), vadd(4, COL(0, 2), COL(0, 0), COL(0, 1)), svq(COL(0, 2), t0, 32),
                vsub(4, COL(0, 3), COL(0, 1), COL(0, 0)), svq(COL(0, 3), t0, 48), vmul(4, COL(1, 0), COL(0, 0), COL(0, 1)), svq(COL(1, 0), t0, 64),
                vdiv(4, COL(1, 1), COL(0, 1), COL(0, 0)), svq(COL(1, 1), t0, 80)}));
    CHECK(v.get(32) == 11 && v.get(36) == 22 && v.get(40) == 33 && v.get(44) == 44);
    CHECK(v.get(48) == 9 && v.get(52) == 18 && v.get(56) == 27 && v.get(60) == 36);
    CHECK(v.get(64) == 10 && v.get(68) == 40 && v.get(72) == 90 && v.get(76) == 160);
    CHECK(v.get(80) == 10 && v.get(84) == 10 && v.get(88) == 10 && v.get(92) == 10); }
  { Vm v; v.put(0, {1, 2, 3, 4}); v.put(16, {10, 20, 30, 40});  // vadd.p writes two lanes, .t three, leaving the rest alone
    v.put(32, {-1, -1, -1, -1});
    CHECK(v.ok({lvq(COL(0, 0), t0, 0), lvq(COL(0, 1), t0, 16), lvq(COL(0, 2), t0, 32), vadd(2, COL(0, 2), COL(0, 0), COL(0, 1)), svq(COL(0, 2), t0, 48),
                lvq(COL(0, 2), t0, 32), vadd(3, COL(0, 2), COL(0, 0), COL(0, 1)), svq(COL(0, 2), t0, 64)}));
    CHECK(v.get(48) == 11 && v.get(52) == 22 && v.get(56) == -1 && v.get(60) == -1);
    CHECK(v.get(64) == 11 && v.get(68) == 22 && v.get(72) == 33 && v.get(76) == -1); }
  { Vm v; v.put(0, {1, 2, 3, 4}); v.put(16, {5, 6, 7, 8});  // dot products and scale
    CHECK(v.ok({lvq(COL(0, 0), t0, 0), lvq(COL(0, 1), t0, 16), vdot(4, SGL(1, 0, 0), COL(0, 0), COL(0, 1)), svs(SGL(1, 0, 0), t0, 32),
                vdot(3, SGL(1, 1, 0), COL(0, 0), COL(0, 1)), svs(SGL(1, 1, 0), t0, 36), vhdp(4, SGL(1, 2, 0), COL(0, 0), COL(0, 1)), svs(SGL(1, 2, 0), t0, 40),
                vscl(4, COL(2, 0), COL(0, 0), SGL(1, 0, 0)), svq(COL(2, 0), t0, 48)}));
    CHECK(v.get(32) == 70 && v.get(36) == 38 && v.get(40) == 46);
    CHECK(v.get(48) == 70 && v.get(52) == 140 && v.get(56) == 210 && v.get(60) == 280); }
  { Vm v; v.put(0, {-4, 0.25f, 9, 2});  // unary group
    CHECK(v.ok({lvq(COL(0, 0), t0, 0), vun(VMOV, 4, COL(0, 1), COL(0, 0)), svq(COL(0, 1), t0, 16), vun(VABS, 4, COL(0, 1), COL(0, 0)), svq(COL(0, 1), t0, 32),
                vun(VNEG, 4, COL(0, 1), COL(0, 0)), svq(COL(0, 1), t0, 48), vun(VSAT0, 4, COL(0, 1), COL(0, 0)), svq(COL(0, 1), t0, 64),
                vun(VSAT1, 4, COL(0, 1), COL(0, 0)), svq(COL(0, 1), t0, 80), vun(VRCP, 4, COL(0, 1), COL(0, 0)), svq(COL(0, 1), t0, 96),
                vun(VSQRT, 4, COL(0, 1), COL(0, 1)), svq(COL(0, 1), t0, 112)}));
    CHECK(v.get(16) == -4 && v.get(20) == 0.25f && v.get(24) == 9 && v.get(28) == 2);
    CHECK(v.get(32) == 4 && v.get(36) == 0.25f && v.get(40) == 9 && v.get(44) == 2);
    CHECK(v.get(48) == 4 && v.get(52) == -0.25f && v.get(56) == -9 && v.get(60) == -2);
    CHECK(v.get(64) == 0 && v.get(68) == 0.25f && v.get(72) == 1 && v.get(76) == 1);
    CHECK(v.get(80) == -1 && v.get(84) == 0.25f && v.get(88) == 1 && v.get(92) == 1);
    CHECK(v.get(96) == -0.25f && v.get(100) == 4 && near(v.get(104), 1.0f / 9, 1e-7f) && v.get(108) == 0.5f);
    CHECK(v.get(112) != v.get(112) && v.get(116) == 2.0f && near(v.get(120), 1.0f / 3, 1e-7f) && near(v.get(124), std::sqrt(0.5f), 1e-7f)); }  // sqrt of the vrcp results; sqrt(-0.25) is NaN
  { Vm v; v.put(0, {16, 4, 2, 100});  // vrsq / vsqrt / vzero / vone
    CHECK(v.ok({lvq(COL(0, 0), t0, 0), vun(VSQRT, 4, COL(0, 1), COL(0, 0)), svq(COL(0, 1), t0, 16), vun(VRSQ, 2, COL(0, 2), COL(0, 0)), svq(COL(0, 2), t0, 32),
                vun(VZERO, 4, COL(0, 3), COL(0, 0)), svq(COL(0, 3), t0, 48), vun(VONE, 3, COL(1, 0), COL(0, 0)), svq(COL(1, 0), t0, 64)}));
    CHECK(v.get(16) == 4 && v.get(20) == 2 && near(v.get(24), std::sqrt(2.0f)) && v.get(28) == 10);
    CHECK(v.get(32) == 0.25f && v.get(36) == 0.5f);
    CHECK(v.get(48) == 0 && v.get(52) == 0 && v.get(56) == 0 && v.get(60) == 0);
    CHECK(v.get(64) == 1 && v.get(68) == 1 && v.get(72) == 1); }
  { Vm v; v.put(0, {0, 1, 2, 3}); v.put(16, {0.5f, 4.5f, -1, -0.5f});  // sin/cos are exact at whole quarter turns
    CHECK(v.ok({lvq(COL(0, 0), t0, 0), vun(VSIN, 4, COL(0, 2), COL(0, 0)), svq(COL(0, 2), t0, 32), vun(VCOS, 4, COL(0, 2), COL(0, 0)), svq(COL(0, 2), t0, 48),
                lvq(COL(0, 1), t0, 16), vun(VSIN, 4, COL(0, 2), COL(0, 1)), svq(COL(0, 2), t0, 64)}));
    CHECK(v.get(32) == 0 && v.get(36) == 1 && v.get(40) == 0 && v.get(44) == -1);
    CHECK(v.get(48) == 1 && v.get(52) == 0 && v.get(56) == -1 && v.get(60) == 0);
    CHECK(near(v.get(64), 0.70710678f, 1e-6f) && near(v.get(68), 0.70710678f, 1e-6f) && near(v.get(72), -1.0f) && near(v.get(76), -0.70710678f, 1e-6f)); }
  std::puts("ok VFPU memory, register views, arithmetic at every size, dot/scale, unary group, sin/cos");

  { Vm v; v.put(0, {1, -2, 3, -4});  // source prefix: swizzle, abs, negate
    CHECK(v.ok({lvq(COL(0, 0), t0, 0), pfx(0, 0x1B), vun(VMOV, 4, COL(0, 1), COL(0, 0)), svq(COL(0, 1), t0, 16),                        // reverse lanes
                pfx(0, 0x00E4 | (0x5u << 8)), vun(VMOV, 4, COL(0, 1), COL(0, 0)), svq(COL(0, 1), t0, 32),                           // abs on lanes 0 and 2
                pfx(0, 0x00E4 | (0xAu << 16)), vun(VMOV, 4, COL(0, 1), COL(0, 0)), svq(COL(0, 1), t0, 48)}));                       // negate lanes 1 and 3
    CHECK(v.get(16) == -4 && v.get(20) == 3 && v.get(24) == -2 && v.get(28) == 1);
    CHECK(v.get(32) == 1 && v.get(36) == -2 && v.get(40) == 3 && v.get(44) == -4);
    CHECK(v.get(48) == 1 && v.get(52) == 2 && v.get(56) == 3 && v.get(60) == 4); }
  { Vm v; v.put(0, {9, 9, 9, 9});  // constants through the prefix, both constant sets
    CHECK(v.ok({lvq(COL(0, 0), t0, 0), pfx(0, (0xFu << 12) | (1u << 2) | (2u << 4) | (3u << 6)), vun(VMOV, 4, COL(0, 1), COL(0, 0)), svq(COL(0, 1), t0, 16),
                pfx(0, (0xFu << 12) | (0xFu << 8) | (1u << 2) | (2u << 4) | (3u << 6)), vun(VMOV, 4, COL(0, 1), COL(0, 0)), svq(COL(0, 1), t0, 32)}));
    CHECK(v.get(16) == 0 && v.get(20) == 1 && v.get(24) == 2 && v.get(28) == 0.5f);
    CHECK(v.get(32) == 3 && near(v.get(36), 1.0f / 3) && v.get(40) == 0.25f && near(v.get(44), 1.0f / 6)); }
  { Vm v; v.put(0, {2, -5, 7, 9}); v.put(16, {100, 100, 100, 100});  // destination prefix: saturation and write mask
    CHECK(v.ok({lvq(COL(0, 0), t0, 0), lvq(COL(0, 1), t0, 16), pfx(2, 1u | (3u << 2) | (1u << 10)), vun(VMOV, 4, COL(0, 1), COL(0, 0)), svq(COL(0, 1), t0, 32)}));
    CHECK(v.get(32) == 1 && v.get(36) == -1 && v.get(40) == 100 && v.get(44) == 9); }
  { Vm v; v.put(0, {1, 2, 3, 4}); v.put(16, {10, 20, 30, 40});  // T prefix; prefixes last for ONE arithmetic op; loads/stores keep them pending
    CHECK(v.ok({lvq(COL(0, 0), t0, 0), lvq(COL(0, 1), t0, 16), pfx(1, 0x1B), lvq(COL(0, 3), t0, 0), vadd(4, COL(0, 2), COL(0, 0), COL(0, 1)), svq(COL(0, 2), t0, 32),
                vadd(4, COL(0, 2), COL(0, 0), COL(0, 1)), svq(COL(0, 2), t0, 48)}));
    CHECK(v.get(32) == 41 && v.get(36) == 32 && v.get(40) == 23 && v.get(44) == 14);  // t reversed on the first add
    CHECK(v.get(48) == 11 && v.get(52) == 22 && v.get(56) == 33 && v.get(60) == 44); }  // gone on the second
  { Vm v; CHECK(v.go({pfx(0, 3u << 0 | (3u << 2)), vun(VMOV, 2, COL(0, 1), COL(0, 0))}) == Step::BadInstruction); }  // pair swizzled to lane 3: rejected
  { Vm v; CHECK(v.go({pfx(2, 2u), vun(VMOV, 1, COL(0, 1), COL(0, 0))}) == Step::BadInstruction); }                    // saturation mode 2: rejected
  std::puts("ok VFPU prefixes: swizzle, abs, negate, constants, saturate, mask, one-shot lifetime");

  { Vm v; v.put(0, {0, 0, 0, 0});  // mtv / mfv / viim / lv.s / sv.s with high register bits
    CHECK(v.ok({LUI(t1, 0x3FC0), mtv(t1, SGL(1, 2, 3)), mfv(10, SGL(1, 2, 3)), svs(SGL(1, 2, 3), t0, 0), viim(SGL(0, 0, 0), -3), svs(SGL(0, 0, 0), t0, 4),
                lvs(SGL(2, 1, 2), t0, 0), svs(SGL(2, 1, 2), t0, 8), 0xFFFF0000u, 0xFFFF0320u, 0xFFFF040Du}));
    CHECK(v.cpu.r[10] == 0x3FC00000u && v.cpu.vr[1 * 4 + 2 + 3 * 32] == 1.5f);
    CHECK(v.get(0) == 1.5f && v.get(4) == -3 && v.get(8) == 1.5f && v.cpu.vr[2 * 4 + 1 + 2 * 32] == 1.5f); }
  { uint32_t bads[] = {0x3Cu << 26 /* vmmul group */, 0xDF800000u /* vfim */, (0x34u << 26) | (3u << 21) /* vcst */, 0x48600000u | 128 /* mfvc */,
                       (0x34u << 26) | (3u << 16) /* vidt */, (0x18u << 26) | (2u << 23) /* vsbn */, 0xFFFF1234u /* unknown VFPU7 */, (0x19u << 26) | (5u << 23) /* vcrs */};
    for (uint32_t b : bads) { Vm v; CHECK(v.go({b}) == Step::BadInstruction); CHECK(v.cpu.lastInstr == b); } }
  std::puts("ok VFPU mtv/mfv/viim/lv.s/sv.s, nops, unsupported forms rejected loudly");
  std::puts("ALL VFPU TESTS PASSED");
  return 0;
}
