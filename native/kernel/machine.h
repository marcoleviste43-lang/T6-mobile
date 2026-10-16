// Machine = CPU + guest RAM + HLE kernel state. Single guest thread for now.
#pragma once
#include "../cpu/mips.h"
#include "sha1.h"
#include <deque>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace t6 {

struct Machine;
using Handler = std::function<void(Machine&)>;
enum class HleResult { Handled, Exit, Unimplemented };

class Hle {
public:
  void add(const std::string& name, Handler h);
  // Returns the syscall code a guest stub should use for this NID. Unknown NIDs get a code that
  // reports "unimplemented" (with the NID) when called, never a silent success.
  uint32_t bindNid(uint32_t nid);
  uint32_t bindName(const std::string& name) { return bindNid(nidOf(name)); }
  HleResult dispatch(Machine& m, uint32_t code);
  uint32_t lastNid = 0;          // NID of the most recent dispatched/unimplemented call
  std::string lastName;          // empty when unimplemented
private:
  struct Entry { std::string name; uint32_t nid; Handler fn; };
  std::vector<Entry> entries_;
  std::unordered_map<uint32_t, uint32_t> byNid_;
};

struct RunResult {
  enum Kind { Budget, Exited, Break, MemFault, BadInstruction, UnimplementedSyscall, Deadlock, AllThreadsExited } kind = Budget;
  uint32_t nid = 0;       // for UnimplementedSyscall
  uint32_t addr = 0;      // for MemFault (fault address) / BadInstruction (instruction word)
};

struct Ctx {
  uint32_t r[32] = {}, pc = 0, npc = 4, hi = 0, lo = 0, fpr[32] = {}, fcr31 = 0;
};
enum class TState { Dormant, Ready, Running, WaitDelay, WaitSema, Sleeping, Dead };
struct Thread {
  uint32_t id = 0; std::string name; int prio = 0x20; TState state = TState::Dormant;
  Ctx ctx; uint64_t seq = 0;
  bool timed = false; uint64_t wakeAt = 0;
  uint32_t semaId = 0, semaNeed = 0, wakeups = 0, exitStatus = 0;
  uint32_t entry = 0, stackTop = 0, stackSize = 0;
  bool deleted = false;
};
struct Sema { int32_t count = 0, maxCount = 0; std::vector<uint32_t> waiters; bool live = true; };

struct Machine {
  Memory mem;
  Cpu cpu{&mem};
  Hle hle;
  bool exited = false;

  // Host-provided controller state (PSP button bits, analog 0..255).
  uint32_t buttons = 0; uint8_t lx = 128, ly = 128;
  // Display
  uint32_t dispMode = 0, dispW = 0, dispH = 0; uint64_t vblanks = 0;
  // Virtual time: instruction count / kInstrPerUs plus explicit delays. A rough approximation, not cycle accurate.
  static constexpr uint64_t kInstrPerUs = 222;
  uint64_t instrCount = 0, delayUs = 0;
  uint64_t nowUs() const { return instrCount / kInstrPerUs + delayUs; }
  // User heap (bump allocator; frees do not reuse space yet)
  uint32_t heapLow = 0, heapHigh = 0;
  struct Block { uint32_t addr, size; bool live; };
  std::vector<Block> blocks;

  Machine();
  void setHeap(uint32_t lowStart, uint32_t highEnd);  // also installs the thread-exit stub at lowStart

  // --- threads (single host thread, cooperative switch points at syscalls, priority: lower number runs first) ---
  std::deque<Thread> threads;   // id = index + 1
  std::vector<Sema> semas;      // id = index + 1
  uint32_t curId = 0;           // 0 = no thread model active (raw CPU mode)
  uint32_t exitStub = 0;        // guest address: move a0,v0 ; syscall sceKernelExitThread
  uint64_t seqCounter = 0;
  bool threaded() const { return !threads.empty(); }
  Thread& cur() { return threads[curId - 1]; }
  Thread* thread(uint32_t id) { return (id >= 1 && id <= threads.size() && !threads[id - 1].deleted) ? &threads[id - 1] : nullptr; }
  uint32_t createThread(const std::string& name, uint32_t entry, int prio, uint32_t stackSize);  // 0 on failure
  bool startThread(uint32_t id, uint32_t argSize, uint32_t argp);
  uint32_t startMain(uint32_t entry, uint32_t stackSize, int prio = 0x20);  // creates + starts the first thread
  void makeReady(Thread& t) { t.state = TState::Ready; t.seq = ++seqCounter; }
  void wakeWithResult(Thread& t, uint32_t v0) { t.ctx.r[2] = v0; makeReady(t); }
  RunResult run(uint64_t maxSteps);
private:
  bool ensureCurrent(RunResult& out);
  void saveCtx(Thread& t);
  void loadCtx(Thread& t);
public:
};

void registerCoreServices(Hle& hle);
void registerThreadServices(Hle& hle);

}  // namespace t6
