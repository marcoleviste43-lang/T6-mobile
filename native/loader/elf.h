// Loader for plain (non-relocatable) 32-bit little-endian MIPS ELF executables.
// PSP PRX modules (ET 0xFFA0, relocatable) are rejected for now.
#pragma once
#include "../memory/memory.h"
#include <string>

namespace t6 {

struct ElfInfo {
  uint32_t entry = 0;
  uint32_t lowestAddr = 0, highestAddr = 0;
  int segments = 0;
};

// Returns true on success. On failure, *err says why and guest memory may be partially written.
bool loadElf(const uint8_t* data, size_t size, Memory& mem, ElfInfo& info, std::string* err);

}  // namespace t6
