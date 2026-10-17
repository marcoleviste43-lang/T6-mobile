// PARTIAL VFPU interpreter. Everything here was written from memory of public emulator documentation and has NEVER
// been checked against a real PSP or a real game binary; the tests only prove internal consistency.
//
// Implemented:
//   memory     lv.s sv.s lv.q sv.q (no writeback form), mtv / mfv (vector registers only, not control registers)
//   VFPU0      vadd vsub vdiv                       (vsbn rejected)
//   VFPU1      vmul vdot vscl vhdp                  (vcrs, vdet rejected)
//   VFPU4      vmov vabs vneg vsat0 vsat1 vzero vone vrcp vrsq vsqrt vsin vcos   (vidt, vcst, vexp2, vlog2... rejected)
//   other      vpfxs vpfxt vpfxd (prefixes), viim, vnop / vsync / vflush
// NOT implemented (BadInstruction): matrix ops (vmmul, vtfm*, vhtfm*), vcmp / conditional ops, vfim, vcst, vi2f / vf2i
//   conversions, vrot, vcrs, vmin / vmax, control registers, lvl.q / lvr.q.
// Assumptions: prefixes are consumed only by vector arithmetic (not by loads, stores, mfv, mtv, viim); a source swizzle
//   that selects a lane beyond the operand size is rejected rather than guessed; sin/cos use exact quarter-turn points
//   and double-precision math otherwise, so low bits can differ from hardware.
#include "mips.h"
#include <cmath>
#include <cstring>

namespace t6 {
namespace {
constexpr float kConst[8] = {0.0f, 1.0f, 2.0f, 0.5f, 3.0f, 1.0f / 3.0f, 0.25f, 1.0f / 6.0f};
constexpr double kHalfPi = 1.57079632679489661923;

inline int sizeOf(uint32_t ins) { return (int)(((ins >> 7) & 1) | (((ins >> 15) & 1) << 1)) + 1; }  // 1..4 lanes

// Maps a 7-bit VFPU vector register name to the 128-entry register file. Lane i of the vector lives at out[i].
void regsOf(int n, uint32_t reg, int out[4]) {
  int mtx = (int)((reg >> 2) & 7), col = (int)(reg & 3), row = 0, transpose = (int)((reg >> 5) & 1);
  switch (n) {
    case 1: transpose = 0; row = (int)((reg >> 5) & 3); break;
    case 2: row = (int)((reg >> 5) & 2); break;
    case 3: row = (int)((reg >> 6) & 1); break;
    default: row = (int)((reg >> 5) & 2); break;
  }
  for (int i = 0; i < n; i++) out[i] = mtx * 4 + (transpose ? (((row + i) & 3) + col * 32) : (col + ((row + i) & 3) * 32));
}

float sinq(float x) {  // sin(pi/2 * x), exact at whole quarter turns
  if (!std::isfinite(x)) return std::nanf("");
  double r = std::fmod((double)x, 4.0); if (r < 0) r += 4.0;
  if (r == 0 || r == 2) return 0.0f;
  if (r == 1) return 1.0f;
  if (r == 3) return -1.0f;
  return (float)std::sin(r * kHalfPi);
}
float cosq(float x) {
  if (!std::isfinite(x)) return std::nanf("");
  double r = std::fmod((double)x, 4.0); if (r < 0) r += 4.0;
  if (r == 0) return 1.0f;
  if (r == 2) return -1.0f;
  if (r == 1 || r == 3) return 0.0f;
  return (float)std::cos(r * kHalfPi);
}
}  // namespace

Step Cpu::vfpu(uint32_t ins) {
  const uint32_t op = ins >> 26;
  auto bad = [&]() { lastInstr = ins; return Step::BadInstruction; };
  auto fault = [&](uint32_t a) { faultAddr = a; return Step::MemFault; };
  auto eat = [&]() { vpfxOn[0] = vpfxOn[1] = vpfxOn[2] = false; };

  // Reads an n-lane operand and applies the S (which = 0) or T (which = 1) prefix. False = invalid swizzle.
  auto read = [&](int n, uint32_t reg, int which, float out[4]) -> bool {
    int idx[4]; regsOf(n, reg, idx);
    float in[4] = {0, 0, 0, 0};
    for (int i = 0; i < n; i++) in[i] = vr[idx[i]];
    if (!vpfxOn[which]) { for (int i = 0; i < n; i++) out[i] = in[i]; return true; }
    uint32_t p = vpfx[which];
    for (int i = 0; i < n; i++) {
      uint32_t swz = (p >> (i * 2)) & 3, abs = (p >> (8 + i)) & 1, cst = (p >> (12 + i)) & 1, neg = (p >> (16 + i)) & 1;
      float v;
      if (cst) v = kConst[(abs ? 4 : 0) + swz];
      else { if ((int)swz >= n) return false; v = in[swz]; if (abs) v = std::fabs(v); }
      out[i] = neg ? -v : v;
    }
    return true;
  };
  // Applies the D prefix (saturation, write mask) and stores. False = unsupported saturation mode.
  auto write = [&](int n, uint32_t reg, const float v[4]) -> bool {
    int idx[4]; regsOf(n, reg, idx);
    float o[4] = {v[0], v[1], v[2], v[3]};
    bool keep[4] = {true, true, true, true};
    if (vpfxOn[2]) {
      uint32_t p = vpfx[2];
      for (int i = 0; i < n; i++) {
        uint32_t sat = (p >> (i * 2)) & 3;
        if (sat == 1) o[i] = o[i] < 0 ? 0.0f : (o[i] > 1 ? 1.0f : o[i]);
        else if (sat == 3) o[i] = o[i] < -1 ? -1.0f : (o[i] > 1 ? 1.0f : o[i]);
        else if (sat == 2) return false;
        keep[i] = !((p >> (8 + i)) & 1);
      }
    }
    for (int i = 0; i < n; i++) if (keep[i]) vr[idx[i]] = o[i];
    return true;
  };

  const uint32_t vd = ins & 0x7F, vs = (ins >> 8) & 0x7F, vt = (ins >> 16) & 0x7F;
  const int n = sizeOf(ins);
  float a[4] = {0, 0, 0, 0}, b[4] = {0, 0, 0, 0}, o[4] = {0, 0, 0, 0};

  switch (op) {
    case 0x12: {  // mfv / mtv (COP2)
      uint32_t rs = (ins >> 21) & 31, rt = (ins >> 16) & 31, imm = ins & 0xFF;
      if ((rs != 3 && rs != 7) || imm >= 128) return bad();  // control-register forms are not implemented
      int idx[4]; regsOf(1, imm, idx);
      if (rs == 3) std::memcpy(&r[rt], &vr[idx[0]], 4); else std::memcpy(&vr[idx[0]], &r[rt], 4);
      if (rt == 0) r[0] = 0;
      return Step::Ok;
    }
    case 0x32: case 0x3a: {  // lv.s / sv.s
      uint32_t reg = ((ins >> 16) & 31) | ((ins & 3) << 5), ea = r[(ins >> 21) & 31] + (uint32_t)(int32_t)(int16_t)(ins & 0xFFFC);
      int idx[4]; regsOf(1, reg, idx);
      if (ea & 3) return fault(ea);
      uint32_t w;
      if (op == 0x32) { if (!mem->read32(ea, w)) return fault(ea); std::memcpy(&vr[idx[0]], &w, 4); }
      else { std::memcpy(&w, &vr[idx[0]], 4); if (!mem->write32(ea, w)) return fault(ea); }
      return Step::Ok;
    }
    case 0x36: case 0x3e: {  // lv.q / sv.q (the writeback form, bit 1, is not implemented)
      if (ins & 2) return bad();
      uint32_t reg = ((ins >> 16) & 31) | ((ins & 1) << 5), ea = r[(ins >> 21) & 31] + (uint32_t)(int32_t)(int16_t)(ins & 0xFFFC);
      int idx[4]; regsOf(4, reg, idx);
      if (ea & 15) return fault(ea);
      for (int i = 0; i < 4; i++) {
        uint32_t w;
        if (op == 0x36) { if (!mem->read32(ea + 4 * (uint32_t)i, w)) return fault(ea); std::memcpy(&vr[idx[i]], &w, 4); }
        else { std::memcpy(&w, &vr[idx[i]], 4); if (!mem->write32(ea + 4 * (uint32_t)i, w)) return fault(ea); }
      }
      return Step::Ok;
    }
    case 0x37: {  // prefixes and viim
      uint32_t kind = (ins >> 24) & 3;
      if (kind <= 2) { vpfx[kind] = ins & (kind == 2 ? 0xFFFu : 0xFFFFFu); vpfxOn[kind] = true; return Step::Ok; }
      if (ins & 0x00800000u) return bad();  // vfim (half-float immediate) is not implemented
      int idx[4]; regsOf(1, vt, idx);
      vr[idx[0]] = (float)(int16_t)(ins & 0xFFFF);
      return Step::Ok;
    }
    case 0x3f:  // vnop / vsync / vflush
      return (ins == 0xFFFF0000u || ins == 0xFFFF0320u || ins == 0xFFFF040Du) ? Step::Ok : bad();
    case 0x18: {  // vadd, vsub, vdiv
      uint32_t fn = (ins >> 23) & 7;
      if (fn != 0 && fn != 1 && fn != 7) return bad();
      if (!read(n, vs, 0, a) || !read(n, vt, 1, b)) return bad();
      for (int i = 0; i < n; i++) o[i] = fn == 0 ? a[i] + b[i] : fn == 1 ? a[i] - b[i] : a[i] / b[i];
      if (!write(n, vd, o)) return bad();
      eat(); return Step::Ok;
    }
    case 0x19: {  // vmul, vdot, vscl, vhdp
      uint32_t fn = (ins >> 23) & 7;
      if (fn == 0) {
        if (!read(n, vs, 0, a) || !read(n, vt, 1, b)) return bad();
        for (int i = 0; i < n; i++) o[i] = a[i] * b[i];
        if (!write(n, vd, o)) return bad();
      } else if (fn == 1 || fn == 4) {  // vdot / vhdp: scalar result
        if (n < 2 && fn == 4) return bad();
        if (!read(n, vs, 0, a) || !read(n, vt, 1, b)) return bad();
        float sum = 0;
        for (int i = 0; i < n; i++) sum += (fn == 4 && i == n - 1) ? b[i] : a[i] * b[i];
        o[0] = sum;
        if (!write(1, vd, o)) return bad();
      } else if (fn == 2) {  // vscl: vector times a scalar
        if (!read(n, vs, 0, a) || !read(1, vt, 1, b)) return bad();
        for (int i = 0; i < n; i++) o[i] = a[i] * b[0];
        if (!write(n, vd, o)) return bad();
      } else return bad();
      eat(); return Step::Ok;
    }
    case 0x34: {  // unary group
      if (((ins >> 21) & 31) != 0) return bad();
      uint32_t fn = (ins >> 16) & 31;
      bool noSrc = (fn == 6 || fn == 7);
      if (fn != 0 && fn != 1 && fn != 2 && fn != 4 && fn != 5 && !noSrc && fn != 16 && fn != 17 && fn != 18 && fn != 19 && fn != 22) return bad();
      if (!noSrc && !read(n, vs, 0, a)) return bad();
      for (int i = 0; i < n; i++) {
        float x = a[i];
        switch (fn) {
          case 0: o[i] = x; break;
          case 1: o[i] = std::fabs(x); break;
          case 2: o[i] = -x; break;
          case 4: o[i] = x < 0 ? 0.0f : (x > 1 ? 1.0f : x); break;
          case 5: o[i] = x < -1 ? -1.0f : (x > 1 ? 1.0f : x); break;
          case 6: o[i] = 0.0f; break;
          case 7: o[i] = 1.0f; break;
          case 16: o[i] = 1.0f / x; break;
          case 17: o[i] = 1.0f / std::sqrt(x); break;
          case 18: o[i] = sinq(x); break;
          case 19: o[i] = cosq(x); break;
          default: o[i] = std::sqrt(x); break;  // 22
        }
      }
      if (!write(n, vd, o)) return bad();
      eat(); return Step::Ok;
    }
    default: return bad();
  }
}

}  // namespace t6
