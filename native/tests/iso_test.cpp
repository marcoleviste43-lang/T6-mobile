#include "../loader/iso9660.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace t6;

static void put32(std::vector<uint8_t>& v, size_t o, uint32_t x) {
  for (int i = 0; i < 4; i++) { v[o + i] = x >> (8 * i); v[o + 4 + (3 - i)] = x >> (8 * i); }  // both-endian
}
static size_t rec(std::vector<uint8_t>& img, size_t off, const std::string& name, uint32_t lba, uint32_t size, bool dir) {
  size_t len = 33 + name.size(); if (len & 1) len++;
  img[off] = (uint8_t)len; put32(img, off + 2, lba); put32(img, off + 10, size);
  img[off + 25] = dir ? 2 : 0; img[off + 32] = (uint8_t)name.size();
  std::memcpy(&img[off + 33], name.data(), name.size());
  return off + len;
}
// Layout: s16 PVD, s17 root, s18 PSP_GAME, s19 SYSDIR, s20 PARAM.SFO, s21 EBOOT.BIN
static std::vector<uint8_t> synth(bool evil = false) {
  std::vector<uint8_t> img(22 * 2048, 0);
  uint8_t* pvd = &img[16 * 2048]; pvd[0] = 1; std::memcpy(pvd + 1, "CD001", 5);
  rec(img, 16 * 2048 + 156, std::string(1, '\0'), 17, 2048, true);
  size_t o = 17 * 2048; o = rec(img, o, std::string(1, '\0'), 17, 2048, true); o = rec(img, o, std::string(1, '\1'), 17, 2048, true);
  rec(img, o, "PSP_GAME", 18, 2048, true);
  o = 18 * 2048; o = rec(img, o, std::string(1, '\0'), 18, 2048, true); o = rec(img, o, std::string(1, '\1'), 17, 2048, true);
  o = rec(img, o, "PARAM.SFO;1", 20, 8, false); rec(img, o, "SYSDIR", evil ? 18 : 19, 2048, true);  // evil: loops to itself
  o = 19 * 2048; o = rec(img, o, std::string(1, '\0'), 19, 2048, true); o = rec(img, o, std::string(1, '\1'), 18, 2048, true);
  rec(img, o, "EBOOT.BIN;1", 21, 8, false);
  std::memcpy(&img[20 * 2048], "\0PSF1234", 8); std::memcpy(&img[21 * 2048], "~PSP\0\0\0\0", 8);
  return img;
}

int main() {
  { auto img = IsoImage::open(std::make_shared<MemorySource>(synth()));
    assert(img);
    auto e = img->find("psp_game/sysdir/eboot.bin"); assert(e && e->size == 8 && !e->isDir);
    std::vector<uint8_t> d; assert(img->readFile(*e, d));
    assert(classifyExecutable(d) == ExecKind::EncryptedPsp);
    assert(img->find("PSP_GAME/PARAM.SFO")); assert(!img->find("PSP_GAME/NOPE"));
    std::puts("ok synthetic ISO parse + case-insensitive find + exec classification"); }
  { std::string err; auto bad = synth(); bad[16 * 2048 + 1] = 'X';
    assert(!IsoImage::open(std::make_shared<MemorySource>(bad), &err)); assert(!err.empty());
    std::puts("ok bad magic rejected"); }
  { std::string err; assert(!IsoImage::open(std::make_shared<MemorySource>(std::vector<uint8_t>(1000)), &err));
    std::puts("ok truncated image rejected"); }
  { std::string err; auto r = IsoImage::open(std::make_shared<MemorySource>(synth(true)), &err);
    assert(!r && err.find("deep") != std::string::npos);
    std::puts("ok directory loop bounded"); }
  { auto img = synth(); img.resize(20 * 2048);  // file extents now out of range
    auto r = IsoImage::open(std::make_shared<MemorySource>(img)); assert(r);
    std::vector<uint8_t> d; assert(!r->readFile(*r->find("PSP_GAME/SYSDIR/EBOOT.BIN"), d));
    std::puts("ok out-of-range file read rejected"); }
  assert(classifyExecutable({0x7f, 'E', 'L', 'F'}) == ExecKind::ElfPlain);
  assert(classifyExecutable({0, 'P', 'B', 'P'}) == ExecKind::PbpContainer);
  assert(classifyExecutable({1, 2}) == ExecKind::Unknown);
  std::puts("ok exec magic classification");
  return 0;
}
