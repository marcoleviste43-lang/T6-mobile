// MIPS32 (Allegrex integer subset) interpreter. No FPU/VFPU yet: those opcodes return BadInstruction.
#pragma once
#include "../memory/memory.h"

namespace t6 {

enum class Step { Ok, Syscall, Break, MemFault, BadInstruction };

struct Cpu {
  uint32_t r[32] = {};
  uint32_t pc = 0, npc = 4;  // npc models the branch delay slot
  uint32_t hi = 0, lo = 0;
  uint32_t syscallCode = 0;  // valid after Step::Syscall / Step::Break
  uint32_t faultAddr = 0;    // valid after Step::MemFault
  uint32_t lastInstr = 0;    // valid after Step::BadInstruction
  Memory* mem = nullptr;

  explicit Cpu(Memory* m) : mem(m) {}
  void setPc(uint32_t p) { pc = p; npc = p + 4; }
  // Executes one instruction. On MemFault/BadInstruction, pc is left at the faulting instruction.
  // On Syscall/Break, pc has already advanced past the instruction.
  Step step();
  // Runs until a non-Ok result or maxSteps instructions. Returns Ok if the budget ran out.
  Step run(uint64_t maxSteps, uint64_t* executed = nullptr);
};

}  // namespace t6
