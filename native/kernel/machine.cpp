#include "machine.h"
#include <climits>
#include <cstdint>

namespace t6 {

void Hle::add(const std::string& name, Handler h) {
  uint32_t nid = nidOf(name);
  auto it = byNid_.find(nid);
  if (it != byNid_.end()) { entries_[it->second] = {name, nid, std::move(h)}; return; }
  byNid_[nid] = (uint32_t)entries_.size();
  entries_.push_back({name, nid, std::move(h)});
}

uint32_t Hle::bindNid(uint32_t nid) {
  auto it = byNid_.find(nid);
  if (it != byNid_.end()) return it->second;
  byNid_[nid] = (uint32_t)entries_.size();
  entries_.push_back({"", nid, nullptr});
  return byNid_[nid];
}

HleResult Hle::dispatch(Machine& m, uint32_t code) {
  if (code >= entries_.size()) { lastNid = 0; lastName.clear(); return HleResult::Unimplemented; }
  Entry& e = entries_[code];
  lastNid = e.nid; lastName = e.name;
  if (!e.fn) return HleResult::Unimplemented;
  e.fn(m);
  return m.exited ? HleResult::Exit : HleResult::Handled;
}

Machine::Machine() { registerCoreServices(hle); registerThreadServices(hle); }

void Machine::setHeap(uint32_t lowStart, uint32_t highEnd) {
  heapLow = lowStart; heapHigh = highEnd;
  exitStub = heapLow;
  mem.write32(exitStub, 0x00022021u);                                   // addu a0, v0, zero
  mem.write32(exitStub + 4, (hle.bindName("sceKernelExitThread") << 6) | 0x0c);  // syscall
  heapLow += 0x100;
}

void Machine::saveCtx(Thread& t) {
  std::memcpy(t.ctx.r, cpu.r, sizeof(cpu.r)); std::memcpy(t.ctx.fpr, cpu.fpr, sizeof(cpu.fpr));
  t.ctx.pc = cpu.pc; t.ctx.npc = cpu.npc; t.ctx.hi = cpu.hi; t.ctx.lo = cpu.lo; t.ctx.fcr31 = cpu.fcr31;
}
void Machine::loadCtx(Thread& t) {
  std::memcpy(cpu.r, t.ctx.r, sizeof(cpu.r)); std::memcpy(cpu.fpr, t.ctx.fpr, sizeof(cpu.fpr));
  cpu.pc = t.ctx.pc; cpu.npc = t.ctx.npc; cpu.hi = t.ctx.hi; cpu.lo = t.ctx.lo; cpu.fcr31 = t.ctx.fcr31;
}

uint32_t Machine::createThread(const std::string& name, uint32_t entry, int prio, uint32_t stackSize) {
  stackSize = (stackSize + 0xFF) & ~0xFFu;
  if (stackSize < 0x200 || stackSize > heapHigh - heapLow) return 0;
  heapHigh -= stackSize;
  threads.emplace_back();
  Thread& t = threads.back();
  t.id = (uint32_t)threads.size(); t.name = name; t.entry = entry; t.prio = prio;
  t.stackSize = stackSize; t.stackTop = heapHigh + stackSize;
  return t.id;
}

bool Machine::startThread(uint32_t id, uint32_t argSize, uint32_t argp) {
  Thread* t = thread(id);
  if (!t || t->state != TState::Dormant) return false;
  uint32_t argRoom = (argSize + 15) & ~15u;
  if (argRoom + 64 > t->stackSize) return false;
  uint32_t sp = t->stackTop - argRoom;
  uint32_t argAddr = 0;
  if (argSize && argp) {
    std::vector<uint8_t> tmp(argSize);
    for (uint32_t i = 0; i < argSize; i++) if (!mem.read8(argp + i, tmp[i])) return false;
    if (!mem.writeBytes(sp, tmp.data(), argSize)) return false;
    argAddr = sp;
  }
  sp -= 64;
  t->ctx = Ctx{};
  t->ctx.r[29] = sp; t->ctx.r[31] = exitStub; t->ctx.r[4] = argSize; t->ctx.r[5] = argAddr;
  t->ctx.r[28] = (curId ? cpu.r[28] : 0);  // assumption: new threads inherit the starter's $gp
  t->ctx.pc = t->entry; t->ctx.npc = t->entry + 4;
  makeReady(*t);
  return true;
}

uint32_t Machine::startMain(uint32_t entry, uint32_t stackSize, int prio) {
  uint32_t id = createThread("main", entry, prio, stackSize);
  if (!id || !startThread(id, 0, 0)) return 0;
  return id;
}

bool Machine::ensureCurrent(RunResult& out) {
  for (;;) {
    uint64_t now = nowUs();
    for (Thread& t : threads) {
      if (t.deleted || !t.timed || now < t.wakeAt) continue;
      if (t.state == TState::WaitDelay) { t.timed = false; wakeWithResult(t, 0); }
      else if (t.state == TState::WaitSema) {  // semaphore wait timed out
        auto& w = semas[t.semaId - 1].waiters;
        for (size_t i = 0; i < w.size(); i++) if (w[i] == t.id) { w.erase(w.begin() + i); break; }
        t.timed = false; wakeWithResult(t, 0x80000001u);  // placeholder timeout code
      }
    }
    Thread* best = nullptr;
    for (Thread& t : threads) {
      if (t.deleted || (t.state != TState::Ready && t.state != TState::Running)) continue;
      if (!best || t.prio < best->prio || (t.prio == best->prio && t.seq < best->seq)) best = &t;
    }
    if (best) {
      if (best->id != curId) {
        if (curId) { Thread& c = cur(); if (c.state != TState::Dead) saveCtx(c); if (c.state == TState::Running) c.state = TState::Ready; }
        curId = best->id; loadCtx(*best);
      }
      best->state = TState::Running;
      return true;
    }
    uint64_t earliest = UINT64_MAX; bool anyAlive = false;
    for (Thread& t : threads) {
      if (t.deleted || t.state == TState::Dead || t.state == TState::Dormant) continue;
      anyAlive = true;
      if (t.timed && t.wakeAt < earliest) earliest = t.wakeAt;
    }
    if (earliest != UINT64_MAX) { delayUs += earliest - now; continue; }  // idle: skip virtual time forward
    out = {anyAlive ? RunResult::Deadlock : RunResult::AllThreadsExited, 0, 0};
    return false;
  }
}

RunResult Machine::run(uint64_t maxSteps) {
  uint64_t left = maxSteps;
  while (left > 0) {
    uint64_t n = 0;
    uint64_t slice = left;
    if (threaded()) {
      RunResult stop;
      if (!ensureCurrent(stop)) return stop;
      if (slice > 4096) slice = 4096;  // re-check timers regularly
    }
    Step s = cpu.run(slice, &n);
    left -= n; instrCount += n;
    switch (s) {
      case Step::Ok: break;
      case Step::Syscall: {
        HleResult r = hle.dispatch(*this, cpu.syscallCode);
        if (r == HleResult::Exit) return {RunResult::Exited, 0, 0};
        if (r == HleResult::Unimplemented) return {RunResult::UnimplementedSyscall, hle.lastNid, 0};
        break;
      }
      case Step::Break: return {RunResult::Break, 0, 0};
      case Step::MemFault: return {RunResult::MemFault, 0, cpu.faultAddr};
      case Step::BadInstruction: return {RunResult::BadInstruction, 0, cpu.lastInstr};
    }
  }
  return {RunResult::Budget, 0, 0};
}

}  // namespace t6
