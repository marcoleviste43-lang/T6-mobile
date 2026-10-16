// Usage: t6_inspect game.iso
// Prints the ISO file list, PARAM.SFO fields and the executable header type.
// Reads only metadata and the first 16 bytes of the executable. Nothing is copied or written.
#include "../loader/iso9660.h"
#include <cctype>
#include <cstdio>
#include <cstring>
using namespace t6;

static uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }
static uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }

static void printSfo(const std::vector<uint8_t>& d) {
  if (d.size() < 20 || std::memcmp(d.data(), "\0PSF", 4) != 0) { std::puts("  PARAM.SFO: not a valid SFO"); return; }
  uint32_t kt = rd32(&d[8]), dt = rd32(&d[12]), n = rd32(&d[16]);
  if (n > 256) n = 256;
  for (uint32_t i = 0; i < n; i++) {
    size_t off = 20 + (size_t)i * 16;
    if (off + 16 > d.size()) break;
    uint16_t ko = rd16(&d[off]), fmt = rd16(&d[off + 2]);
    uint32_t len = rd32(&d[off + 4]), doff = rd32(&d[off + 12]);
    size_t ks = (size_t)kt + ko, ds = (size_t)dt + doff;
    if (ks >= d.size() || ds > d.size() || len > d.size() - ds) continue;
    std::string key;
    for (size_t k = ks; k < d.size() && d[k] && key.size() < 32; k++) key += (char)d[k];
    if (fmt == 0x0404 && len >= 4) { std::printf("  %-16s = %u\n", key.c_str(), rd32(&d[ds])); continue; }
    std::string val;
    for (uint32_t k = 0; k < len && d[ds + k]; k++) val += std::isprint(d[ds + k]) ? (char)d[ds + k] : '?';
    std::printf("  %-16s = %s\n", key.c_str(), val.c_str());
  }
}

static void headerOf(const IsoImage& img, const char* path) {
  auto e = img.find(path);
  if (!e) { std::printf("  %-28s not present\n", path); return; }
  std::vector<uint8_t> d;
  if (!img.readFile(*e, d) || d.empty()) { std::printf("  %-28s present, unreadable\n", path); return; }
  std::printf("  %-28s %u bytes, type: %s, first bytes:", path, e->size, toString(classifyExecutable(d)));
  for (size_t i = 0; i < 16 && i < d.size(); i++) std::printf(" %02x", d[i]);
  std::puts("");
}

int main(int argc, char** argv) {
  if (argc != 2) { std::fprintf(stderr, "usage: %s game.iso\n", argv[0]); return 2; }
  auto src = std::make_shared<FileSource>(argv[1]);
  if (!src->ok()) { std::fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
  std::string err;
  auto img = IsoImage::open(src, &err);
  if (!img) { std::fprintf(stderr, "not a readable ISO9660 image: %s\n", err.c_str()); return 1; }
  std::printf("image size: %llu bytes, %zu entries\n\n", (unsigned long long)src->size(), img->entries().size());
  std::puts("top-level:");
  for (auto& e : img->entries())
    if (e.path.find('/') == std::string::npos) std::printf("  %s%s  (%u bytes)\n", e.path.c_str(), e.isDir ? "/" : "", e.size);
  std::puts("\nPSP_GAME/SYSDIR:");
  for (auto& e : img->entries())
    if (e.path.rfind("PSP_GAME/SYSDIR/", 0) == 0 && e.path.find('/', 16) == std::string::npos) std::printf("  %s  (%u bytes)\n", e.path.c_str() + 16, e.size);
  std::puts("\nPARAM.SFO:");
  if (auto e = img->find("PSP_GAME/PARAM.SFO")) { std::vector<uint8_t> d; if (img->readFile(*e, d)) printSfo(d); else std::puts("  unreadable"); }
  else std::puts("  not present");
  std::puts("\nexecutables:");
  headerOf(*img, "PSP_GAME/SYSDIR/EBOOT.BIN");
  headerOf(*img, "PSP_GAME/SYSDIR/BOOT.BIN");
  return 0;
}
