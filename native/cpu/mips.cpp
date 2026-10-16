#include "mips.h"
#include <cmath>
#include <cstring>

namespace t6 {
namespace {
inline uint32_t maskOf(uint32_t size) { return size >= 32 ? 0xFFFFFFFFu : ((1u << size) - 1); }
// mode: 0 nearest-even, 1 toward zero, 2 toward +inf, 3 toward -inf. Out-of-range/NaN saturation is an assumption.
uint32_t toWord(float x, uint32_t mode) {
  if (std::isnan(x)) return 0x7FFFFFFFu;
  double d = x;
  switch (mode & 3) {
    case 0: d = std::nearbyint(d); break;
    case 1: d = std::trunc(d); break;
    case 2: d = std::ceil(d); break;
    default: d = std::floor(d); break;
  }
  if (d >= 2147483648.0) return 0x7FFFFFFFu;
  if (d < -2147483648.0) return 0x80000000u;
  return (uint32_t)(int32_t)d;
}
inline uint32_t bswap32(uint32_t x) { return (x >> 24) | ((x >> 8) & 0xFF00) | ((x << 8) & 0xFF0000) | (x << 24); }
}  // namespace

Step Cpu::step() {
  uint32_t ins;
  if ((pc & 3) || !mem->read32(pc, ins)) { faultAddr = pc; return Step::MemFault; }
  const uint32_t op = ins >> 26, rs = (ins >> 21) & 31, rt = (ins >> 16) & 31, rd = (ins >> 11) & 31, sa = (ins >> 6) & 31, fn = ins & 63;
  const int32_t simm = (int16_t)(ins & 0xFFFF);
  const uint32_t uimm = ins & 0xFFFF;
  const uint32_t a = r[rs], b = r[rt];
  const int32_t sa_ = (int32_t)a, sb_ = (int32_t)b;

  uint32_t newPc = npc, newNpc = npc + 4;
  auto branch = [&](bool cond, bool likely) {
    if (cond) newNpc = pc + 4 + ((uint32_t)simm << 2);
    else if (likely) { newPc = npc + 4; newNpc = npc + 8; }  // delay slot nullified
  };
  auto bad = [&]() { lastInstr = ins; return Step::BadInstruction; };
  auto fault = [&](uint32_t addr) { faultAddr = addr; return Step::MemFault; };
  auto commit = [&]() { pc = newPc; npc = newNpc; r[0] = 0; };

  switch (op) {
    case 0x00:  // SPECIAL
      switch (fn) {
        case 0x00: r[rd] = b << sa; break;
        case 0x02: r[rd] = (rs & 1) ? ((b >> sa) | (b << ((32 - sa) & 31))) : (b >> sa); break;  // srl / rotr
        case 0x03: r[rd] = (uint32_t)(sb_ >> sa); break;
        case 0x04: r[rd] = b << (a & 31); break;
        case 0x06: r[rd] = (sa & 1) ? ((b >> (a & 31)) | (b << ((32 - (a & 31)) & 31))) : (b >> (a & 31)); break;  // srlv / rotrv
        case 0x07: r[rd] = (uint32_t)(sb_ >> (a & 31)); break;
        case 0x08: newNpc = a; break;                              // jr
        case 0x09: r[rd] = pc + 8; newNpc = a; break;              // jalr
        case 0x0a: if (b == 0) r[rd] = a; break;                   // movz
        case 0x0b: if (b != 0) r[rd] = a; break;                   // movn
        case 0x0c: syscallCode = (ins >> 6) & 0xFFFFF; commit(); return Step::Syscall;
        case 0x0d: syscallCode = (ins >> 6) & 0xFFFFF; commit(); return Step::Break;
        case 0x0f: break;                                          // sync
        case 0x10: r[rd] = hi; break;
        case 0x11: hi = a; break;
        case 0x12: r[rd] = lo; break;
        case 0x13: lo = a; break;
        case 0x16: r[rd] = a ? (uint32_t)__builtin_clz(a) : 32; break;       // clz
        case 0x17: r[rd] = ~a ? (uint32_t)__builtin_clz(~a) : 32; break;     // clo
        case 0x18: { int64_t p = (int64_t)sa_ * sb_; lo = (uint32_t)p; hi = (uint32_t)((uint64_t)p >> 32); break; }
        case 0x19: { uint64_t p = (uint64_t)a * b; lo = (uint32_t)p; hi = (uint32_t)(p >> 32); break; }
        case 0x1a:
          if (sb_ == 0) { lo = sa_ < 0 ? 1 : 0xFFFFFFFFu; hi = a; }
          else if (sa_ == INT32_MIN && sb_ == -1) { lo = a; hi = 0; }
          else { lo = (uint32_t)(sa_ / sb_); hi = (uint32_t)(sa_ % sb_); }
          break;
        case 0x1b:
          if (b == 0) { lo = 0xFFFFFFFFu; hi = a; } else { lo = a / b; hi = a % b; }
          break;
        case 0x1c: case 0x1d: case 0x2e: case 0x2f: {
          uint64_t acc = ((uint64_t)hi << 32) | lo;
          uint64_t p = (fn & 1) ? (uint64_t)a * b : (uint64_t)((int64_t)sa_ * sb_);
          acc = (fn == 0x1c || fn == 0x1d) ? acc + p : acc - p;
          lo = (uint32_t)acc; hi = (uint32_t)(acc >> 32);
          break;
        }
        case 0x20: case 0x21: r[rd] = a + b; break;  // add/addu (overflow trap not modelled)
        case 0x22: case 0x23: r[rd] = a - b; break;
        case 0x24: r[rd] = a & b; break;
        case 0x25: r[rd] = a | b; break;
        case 0x26: r[rd] = a ^ b; break;
        case 0x27: r[rd] = ~(a | b); break;
        case 0x2a: r[rd] = sa_ < sb_; break;
        case 0x2b: r[rd] = a < b; break;
        case 0x2c: r[rd] = (uint32_t)(sa_ > sb_ ? sa_ : sb_); break;  // max
        case 0x2d: r[rd] = (uint32_t)(sa_ < sb_ ? sa_ : sb_); break;  // min
        default: return bad();
      }
      break;
    case 0x01:  // REGIMM
      switch (rt) {
        case 0: branch(sa_ < 0, false); break;
        case 1: branch(sa_ >= 0, false); break;
        case 2: branch(sa_ < 0, true); break;
        case 3: branch(sa_ >= 0, true); break;
        case 16: r[31] = pc + 8; branch(sa_ < 0, false); break;
        case 17: r[31] = pc + 8; branch(sa_ >= 0, false); break;
        case 18: r[31] = pc + 8; branch(sa_ < 0, true); break;
        case 19: r[31] = pc + 8; branch(sa_ >= 0, true); break;
        default: return bad();
      }
      break;
    case 0x02: newNpc = ((pc + 4) & 0xF0000000u) | ((ins & 0x3FFFFFF) << 2); break;
    case 0x03: r[31] = pc + 8; newNpc = ((pc + 4) & 0xF0000000u) | ((ins & 0x3FFFFFF) << 2); break;
    case 0x04: branch(a == b, false); break;
    case 0x05: branch(a != b, false); break;
    case 0x06: branch(sa_ <= 0, false); break;
    case 0x07: branch(sa_ > 0, false); break;
    case 0x08: case 0x09: r[rt] = a + (uint32_t)simm; break;
    case 0x0a: r[rt] = sa_ < simm; break;
    case 0x0b: r[rt] = a < (uint32_t)simm; break;
    case 0x0c: r[rt] = a & uimm; break;
    case 0x0d: r[rt] = a | uimm; break;
    case 0x0e: r[rt] = a ^ uimm; break;
    case 0x0f: r[rt] = uimm << 16; break;
    case 0x14: branch(a == b, true); break;
    case 0x15: branch(a != b, true); break;
    case 0x16: branch(sa_ <= 0, true); break;
    case 0x17: branch(sa_ > 0, true); break;
    case 0x1f:  // SPECIAL3
      if (fn == 0x00) { r[rt] = (a >> sa) & maskOf(rd + 1); }                      // ext
      else if (fn == 0x04) {                                                       // ins
        if (rd < sa) return bad();
        uint32_t m = maskOf(rd - sa + 1);
        r[rt] = (b & ~(m << sa)) | ((a & m) << sa);
      } else if (fn == 0x20) {                                                     // BSHFL
        switch (sa) {
          case 0x02: r[rd] = ((b & 0xFF00FF00u) >> 8) | ((b & 0x00FF00FFu) << 8); break;  // wsbh
          case 0x03: r[rd] = bswap32(b); break;                                          // wsbw
          case 0x10: r[rd] = (uint32_t)(int32_t)(int8_t)b; break;                        // seb
          case 0x14: { uint32_t x = 0; for (int i = 0; i < 32; i++) x |= ((b >> i) & 1u) << (31 - i); r[rd] = x; break; }  // bitrev
          case 0x18: r[rd] = (uint32_t)(int32_t)(int16_t)b; break;                       // seh
          default: return bad();
        }
      } else return bad();
      break;
    case 0x20: { uint8_t v; uint32_t ea = a + simm; if (!mem->read8(ea, v)) return fault(ea); r[rt] = (uint32_t)(int32_t)(int8_t)v; break; }
    case 0x21: { uint16_t v; uint32_t ea = a + simm; if ((ea & 1) || !mem->read16(ea, v)) return fault(ea); r[rt] = (uint32_t)(int32_t)(int16_t)v; break; }
    case 0x22: {  // lwl
      uint32_t ea = a + simm, w, off = ea & 3;
      if (!mem->read32(ea & ~3u, w)) return fault(ea);
      r[rt] = (b & (0x00FFFFFFu >> (off * 8))) | (w << (24 - off * 8)); break;
    }
    case 0x23: case 0x30: { uint32_t v, ea = a + simm; if ((ea & 3) || !mem->read32(ea, v)) return fault(ea); r[rt] = v; break; }  // lw / ll
    case 0x24: { uint8_t v; uint32_t ea = a + simm; if (!mem->read8(ea, v)) return fault(ea); r[rt] = v; break; }
    case 0x25: { uint16_t v; uint32_t ea = a + simm; if ((ea & 1) || !mem->read16(ea, v)) return fault(ea); r[rt] = v; break; }
    case 0x26: {  // lwr
      uint32_t ea = a + simm, w, off = ea & 3;
      if (!mem->read32(ea & ~3u, w)) return fault(ea);
      r[rt] = (b & (0xFFFFFF00u << (24 - off * 8))) | (w >> (off * 8)); break;
    }
    case 0x28: { uint32_t ea = a + simm; if (!mem->write8(ea, (uint8_t)b)) return fault(ea); break; }
    case 0x29: { uint32_t ea = a + simm; if ((ea & 1) || !mem->write16(ea, (uint16_t)b)) return fault(ea); break; }
    case 0x2a: {  // swl
      uint32_t ea = a + simm, w, off = ea & 3;
      if (!mem->read32(ea & ~3u, w)) return fault(ea);
      if (!mem->write32(ea & ~3u, (w & (0xFFFFFF00u << (off * 8))) | (b >> (24 - off * 8)))) return fault(ea);
      break;
    }
    case 0x2b: { uint32_t ea = a + simm; if ((ea & 3) || !mem->write32(ea, b)) return fault(ea); break; }
    case 0x2e: {  // swr
      uint32_t ea = a + simm, w, off = ea & 3;
      if (!mem->read32(ea & ~3u, w)) return fault(ea);
      if (!mem->write32(ea & ~3u, (w & (0x00FFFFFFu >> (24 - off * 8))) | (b << (off * 8)))) return fault(ea);
      break;
    }
    case 0x38: { uint32_t ea = a + simm; if ((ea & 3) || !mem->write32(ea, b)) return fault(ea); r[rt] = 1; break; }  // sc (single core: always succeeds)
    case 0x11: {  // COP1 (single precision only)
      const uint32_t fs = rd, ft = rt, fd = sa;
      auto F = [&](uint32_t i) { float x; std::memcpy(&x, &fpr[i], 4); return x; };
      auto SF = [&](uint32_t i, float x) { std::memcpy(&fpr[i], &x, 4); };
      switch (rs) {
        case 0x00: r[rt] = fpr[fs]; break;                      // mfc1
        case 0x02: r[rt] = (rd == 31) ? fcr31 : 0; break;       // cfc1
        case 0x04: fpr[fs] = b; break;                          // mtc1
        case 0x06: if (rd == 31) fcr31 = b & 0x01FFFFFFu; break;  // ctc1
        case 0x08: {                                            // bc1f/t/fl/tl
          bool c = (fcr31 >> 23) & 1;
          switch (rt) {
            case 0: branch(!c, false); break;
            case 1: branch(c, false); break;
            case 2: branch(!c, true); break;
            case 3: branch(c, true); break;
            default: return bad();
          }
          break;
        }
        case 0x10: {  // fmt S
          float x = F(fs), y = F(ft);
          if (fn >= 0x30) {  // c.cond.s
            bool un = std::isnan(x) || std::isnan(y);
            bool res = ((fn & 4) && x < y) || ((fn & 2) && x == y) || ((fn & 1) && un);
            fcr31 = res ? (fcr31 | (1u << 23)) : (fcr31 & ~(1u << 23));
            break;
          }
          switch (fn) {
            case 0x00: SF(fd, x + y); break;
            case 0x01: SF(fd, x - y); break;
            case 0x02: SF(fd, x * y); break;
            case 0x03: SF(fd, x / y); break;
            case 0x04: SF(fd, std::sqrt(x)); break;
            case 0x05: fpr[fd] = fpr[fs] & 0x7FFFFFFFu; break;
            case 0x06: fpr[fd] = fpr[fs]; break;
            case 0x07: fpr[fd] = fpr[fs] ^ 0x80000000u; break;
            case 0x0c: fpr[fd] = toWord(x, 0); break;
            case 0x0d: fpr[fd] = toWord(x, 1); break;
            case 0x0e: fpr[fd] = toWord(x, 2); break;
            case 0x0f: fpr[fd] = toWord(x, 3); break;
            case 0x24: fpr[fd] = toWord(x, fcr31 & 3); break;  // cvt.w.s
            default: return bad();
          }
          break;
        }
        case 0x14:  // fmt W
          if (fn == 0x20) SF(fd, (float)(int32_t)fpr[fs]);  // cvt.s.w
          else return bad();
          break;
        default: return bad();  // includes fmt D (no double precision on Allegrex)
      }
      break;
    }
    case 0x31: { uint32_t v, ea = a + simm; if ((ea & 3) || !mem->read32(ea, v)) return fault(ea); fpr[rt] = v; break; }   // lwc1
    case 0x39: { uint32_t ea = a + simm; if ((ea & 3) || !mem->write32(ea, fpr[rt])) return fault(ea); break; }          // swc1
    case 0x2f: case 0x33: break;  // cache, pref
    default: return bad();
  }
  commit();
  return Step::Ok;
}

Step Cpu::run(uint64_t maxSteps, uint64_t* executed) {
  uint64_t n = 0;
  Step s = Step::Ok;
  while (n < maxSteps) { s = step(); n++; if (s != Step::Ok) break; }
  if (executed) *executed = n;
  return s;
}

}  // namespace t6
