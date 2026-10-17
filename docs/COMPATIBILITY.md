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
