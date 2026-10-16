#include "elf.h"
#include <cstring>

namespace t6 {
namespace {
uint16_t u16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t u32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }
bool fail(std::string* e, const char* m) { if (e) *e = m; return false; }
}  // namespace

bool loadElf(const uint8_t* d, size_t size, Memory& mem, ElfInfo& info, std::string* err) {
  if (size < 52 || std::memcmp(d, "\x7f" "ELF", 4) != 0) return fail(err, "not an ELF file");
  if (d[4] != 1 || d[5] != 1) return fail(err, "not 32-bit little-endian");
  uint16_t type = u16(d + 16), machine = u16(d + 18);
  if (machine != 8) return fail(err, "not a MIPS ELF");
  if (type == 0xFFA0) return fail(err, "relocatable PSP PRX is not supported yet");
  if (type != 2) return fail(err, "not an ET_EXEC executable");
  uint32_t entry = u32(d + 24), phoff = u32(d + 28);
  uint16_t phentsize = u16(d + 42), phnum = u16(d + 44);
  if (phentsize < 32 || phnum == 0 || phnum > 64) return fail(err, "bad program header table");
  if (phoff > size || (uint64_t)phnum * phentsize > size - phoff) return fail(err, "program headers out of range");

  info = ElfInfo{};
  info.entry = entry; info.lowestAddr = 0xFFFFFFFFu;
  for (uint16_t i = 0; i < phnum; i++) {
    const uint8_t* ph = d + phoff + (size_t)i * phentsize;
    if (u32(ph) != 1) continue;  // PT_LOAD only
    uint32_t off = u32(ph + 4), vaddr = u32(ph + 8), filesz = u32(ph + 16), memsz = u32(ph + 20);
    if (filesz > memsz) return fail(err, "segment filesz > memsz");
    if (off > size || filesz > size - off) return fail(err, "segment data out of file range");
    if (!mem.contains(vaddr, memsz)) return fail(err, "segment outside guest RAM");
    if (!mem.writeBytes(vaddr, d + off, filesz)) return fail(err, "segment write failed");
    if (!mem.zero(vaddr + filesz, memsz - filesz)) return fail(err, "bss clear failed");
    if (vaddr < info.lowestAddr) info.lowestAddr = vaddr;
    if (vaddr + memsz > info.highestAddr) info.highestAddr = vaddr + memsz;
    info.segments++;
  }
  if (info.segments == 0) return fail(err, "no loadable segments");
  if (!mem.contains(entry, 4)) return fail(err, "entry point outside guest RAM");
  return true;
}

}  // namespace t6
