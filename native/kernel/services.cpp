// Core HLE services. Error codes marked "placeholder" are NOT verified against real PSP values.
#include "machine.h"

namespace t6 {
namespace {
constexpr uint32_t kErrPlaceholder = 0x80000001u;  // placeholder error code
constexpr uint64_t kVblankUs = 16683;              // ~59.94 Hz

uint32_t& A(Machine& m, int i) { return m.cpu.r[4 + i]; }  // a0..a3 = r4..r7
void ret(Machine& m, uint32_t v) { m.cpu.r[2] = v; }
}  // namespace

void registerCoreServices(Hle& hle) {
  hle.add("sceKernelExitGame", [](Machine& m) { m.exited = true; });

  hle.add("sceDisplaySetMode", [](Machine& m) {
    m.dispMode = A(m, 0); m.dispW = A(m, 1); m.dispH = A(m, 2);
    ret(m, (m.dispW == 0 || m.dispH == 0) ? kErrPlaceholder : 0);
  });
  hle.add("sceDisplayWaitVblankStart", [](Machine& m) {
    uint64_t now = m.nowUs();
    uint64_t wait = kVblankUs - (now % kVblankUs);  // time to the next vblank boundary
    m.vblanks++;
    if (m.threaded()) { Thread& t = m.cur(); t.state = TState::WaitDelay; t.timed = true; t.wakeAt = now + wait; return; }
    m.delayUs += wait;
    ret(m, 0);
  });

  hle.add("sceCtrlSetSamplingCycle", [](Machine& m) { ret(m, 0); });
  hle.add("sceCtrlSetSamplingMode", [](Machine& m) { ret(m, 0); });
  hle.add("sceCtrlReadBufferPositive", [](Machine& m) {
    uint32_t buf = A(m, 0), count = A(m, 1);
    if (count == 0 || count > 64 || !m.mem.contains(buf, count * 16)) { ret(m, kErrPlaceholder); return; }
    for (uint32_t i = 0; i < count; i++) {  // SceCtrlData: u32 timestamp, u32 buttons, u8 lx, u8 ly, u8 reserved[6]
      uint32_t p = buf + i * 16;
      m.mem.write32(p, (uint32_t)m.nowUs()); m.mem.write32(p + 4, m.buttons);
      m.mem.write8(p + 8, m.lx); m.mem.write8(p + 9, m.ly);
      m.mem.zero(p + 10, 6);
    }
    ret(m, count);
  });

  hle.add("sceKernelGetSystemTimeLow", [](Machine& m) { ret(m, (uint32_t)m.nowUs()); });
  hle.add("sceKernelGetSystemTimeWide", [](Machine& m) {
    uint64_t t = m.nowUs(); ret(m, (uint32_t)t); m.cpu.r[3] = (uint32_t)(t >> 32);
  });
  hle.add("sceKernelDelayThread", [](Machine& m) {
    if (m.threaded()) { Thread& t = m.cur(); t.state = TState::WaitDelay; t.timed = true; t.wakeAt = m.nowUs() + A(m, 0); return; }
    m.delayUs += A(m, 0); ret(m, 0);
  });

  // sceKernelAllocPartitionMemory(partition, name, type, size, addr): type 0 = low, 1 = high. Returns block id (>0).
  hle.add("sceKernelAllocPartitionMemory", [](Machine& m) {
    uint32_t type = A(m, 2), size = (A(m, 3) + 0xFF) & ~0xFFu;
    if (size == 0 || type > 1 || size > m.heapHigh - m.heapLow) { ret(m, kErrPlaceholder); return; }
    uint32_t addr;
    if (type == 0) { addr = m.heapLow; m.heapLow += size; }
    else { m.heapHigh -= size; addr = m.heapHigh; }
    m.blocks.push_back({addr, size, true});
    ret(m, (uint32_t)m.blocks.size());
  });
  hle.add("sceKernelGetBlockHeadAddr", [](Machine& m) {
    uint32_t id = A(m, 0);
    ret(m, (id >= 1 && id <= m.blocks.size() && m.blocks[id - 1].live) ? m.blocks[id - 1].addr : 0);
  });
  hle.add("sceKernelFreePartitionMemory", [](Machine& m) {
    uint32_t id = A(m, 0);
    if (id < 1 || id > m.blocks.size() || !m.blocks[id - 1].live) { ret(m, kErrPlaceholder); return; }
    m.blocks[id - 1].live = false; ret(m, 0);
  });
}

}  // namespace t6
