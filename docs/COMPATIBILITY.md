# Compatibility and blockers

Evidence level: none yet from a real ISO.

1. EBOOT decryption (expected, unverified). Retail PSP executables are normally `~PSP`-encrypted. The ISO reader detects this but cannot decrypt. A clean-room KIRK/PRX decryption path is required before any CPU work can run the game. This is the first gate for Phase 5 and needs a developer-supplied ISO to confirm.
2. Ad hoc networking. Which sceNetAdhoc* calls Tekken 6 uses, and its packet timing, are unknown until traced. Do not design the transport around guesses.
3. Rollback feasibility depends on snapshotting full guest state (CPU, RAM, GE, audio). Unknown; delay-based sync is the fallback.

## HLE notes
- NIDs are derived as the first 4 bytes (little-endian) of SHA-1(function name). Verified against four NIDs known from public SDK listings (sceKernelExitGame, sceDisplaySetMode, sceCtrlReadBufferPositive, sceKernelDelayThread).
- Handlers return 0x80000001 as a placeholder error. Real PSP error codes are not verified.
- Unknown imports stop execution and report their NID, so missing services are found by evidence.

## Threads
- Scheduling: lowest priority number runs first; ties by when a thread became ready; switches only at syscalls and timer checks (every 4096 instructions). No timeslicing among equal priorities.
- New threads inherit the starter's $gp (assumption). Thread arguments are copied to the new stack.
- Semaphore waiters are served in strict FIFO order. Callbacks (CB wait variants) are not implemented.
- Idle time is skipped forward; if nothing can ever wake, run() reports Deadlock.

## Event flags and mutexes (unverified)
- Written from public SDK and emulator documentation, never checked against real game behavior.
- Event flag wait mode bits: 0x01 = OR (default AND), 0x10 = clear the whole pattern, 0x20 = clear only the awaited bits. The pattern reported to the caller is the one seen at wake, before clearing.
- `sceKernelClearEventFlag` treats its argument as a keep-mask (pattern &= mask). A timed-out wait leaves the output pointer untouched.
- Mutexes: waiters are served in FIFO order (the priority-order attribute is ignored). Recursive locking needs attribute bit 0x200. A thread that dies while holding a mutex does not release it.
- Error codes are still placeholders.

## Android signing
- The debug APK is signed with a committed key (`android/app/debug.keystore`, public passwords, prototype only). Do not replace or regenerate this file, or updates will conflict again.
- `versionCode` is the CI run number plus 100, so every CI build can install over the previous one.

## VFPU (partial, unverified)
- Written from memory of public emulator documentation; never checked against a real PSP or a real game binary. The tests only prove that the interpreter agrees with the test encoders, which use the same memory.
- Implemented: lv.s, sv.s, lv.q, sv.q, mtv/mfv (vector registers only), vadd, vsub, vdiv, vmul, vdot, vhdp, vscl, vmov, vabs, vneg, vsat0, vsat1, vzero, vone, vrcp, vrsq, vsqrt, vsin, vcos, the three prefixes, viim, vnop/vsync/vflush.
- Not implemented, reported as BadInstruction with the instruction word: all matrix ops (vmmul, vtfm*, vhtfm*), compares and conditional ops, vfim, vcst, int/float conversions, vmin/vmax, vrot, vcrs, vdet, vsbn, vidt, vexp2, vlog2, control registers, lvl.q/lvr.q.
- Register file: 128 floats, index = matrix*4 + column + row*32. Vector names map onto it as column or row vectors by the transpose bit.
- Assumptions to verify first against a real binary: prefixes are consumed only by vector arithmetic (not by loads, stores, mfv, mtv, viim); a source swizzle selecting a lane beyond the operand size is rejected; destination saturation mode 2 is rejected; sin/cos are exact at whole quarter turns and use double-precision math elsewhere, so low bits may differ from hardware; NaN and infinity behavior is whatever the host float does.
