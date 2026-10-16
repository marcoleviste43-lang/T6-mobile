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
