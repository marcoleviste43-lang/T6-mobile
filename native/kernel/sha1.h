// Small SHA-1, used only to derive PSP function NIDs (first 4 digest bytes, little-endian).
#pragma once
#include <cstdint>
#include <cstring>
#include <string>

namespace t6 {

inline void sha1(const uint8_t* data, size_t len, uint8_t out[20]) {
  uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
  std::string msg(reinterpret_cast<const char*>(data), len);
  msg.push_back((char)0x80);
  while (msg.size() % 64 != 56) msg.push_back(0);
  uint64_t bits = (uint64_t)len * 8;
  for (int i = 7; i >= 0; i--) msg.push_back((char)(bits >> (8 * i)));
  auto rol = [](uint32_t x, int n) { return (x << n) | (x >> (32 - n)); };
  for (size_t off = 0; off < msg.size(); off += 64) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++) {
      const uint8_t* p = reinterpret_cast<const uint8_t*>(&msg[off + 4 * i]);
      w[i] = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
    }
    for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) {
      uint32_t f, k;
      if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999u; }
      else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1u; }
      else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
      else { f = b ^ c ^ d; k = 0xCA62C1D6u; }
      uint32_t t = rol(a, 5) + f + e + k + w[i];
      e = d; d = c; c = rol(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
  }
  for (int i = 0; i < 5; i++) for (int j = 0; j < 4; j++) out[4 * i + j] = (uint8_t)(h[i] >> (24 - 8 * j));
}

inline uint32_t nidOf(const std::string& name) {
  uint8_t d[20];
  sha1(reinterpret_cast<const uint8_t*>(name.data()), name.size(), d);
  return d[0] | (d[1] << 8) | (d[2] << 16) | (uint32_t(d[3]) << 24);
}

}  // namespace t6
