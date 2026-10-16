// Flat guest RAM for the PSP user address space (32 MB at 0x08000000). Bounds-checked.
#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

namespace t6 {

class Memory {
public:
  static constexpr uint32_t kBase = 0x08000000u;
  static constexpr uint32_t kSize = 32u * 1024 * 1024;

  Memory() : ram_(kSize, 0) {}
  bool contains(uint32_t addr, uint32_t len) const { return addr >= kBase && len <= kSize && addr - kBase <= kSize - len; }

  bool read8(uint32_t a, uint8_t& v) const { return get(a, v); }
  bool read16(uint32_t a, uint16_t& v) const { return get(a, v); }
  bool read32(uint32_t a, uint32_t& v) const { return get(a, v); }
  bool write8(uint32_t a, uint8_t v) { return put(a, v); }
  bool write16(uint32_t a, uint16_t v) { return put(a, v); }
  bool write32(uint32_t a, uint32_t v) { return put(a, v); }

  bool writeBytes(uint32_t a, const uint8_t* p, uint32_t n) {
    if (!contains(a, n)) return false;
    if (n) std::memcpy(&ram_[a - kBase], p, n);
    return true;
  }
  bool zero(uint32_t a, uint32_t n) {
    if (!contains(a, n)) return false;
    if (n) std::memset(&ram_[a - kBase], 0, n);
    return true;
  }

private:
  template <class T> bool get(uint32_t a, T& v) const {
    if (!contains(a, sizeof(T))) return false;
    std::memcpy(&v, &ram_[a - kBase], sizeof(T));  // host is little-endian (ARM64/x86-64)
    return true;
  }
  template <class T> bool put(uint32_t a, T v) {
    if (!contains(a, sizeof(T))) return false;
    std::memcpy(&ram_[a - kBase], &v, sizeof(T));
    return true;
  }
  std::vector<uint8_t> ram_;
};

}  // namespace t6
