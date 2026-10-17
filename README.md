# TEKKEN 6 NATIVE ONLINE

Multiplayer-only Android app that is meant to run the user's own PSP Tekken 6 ISO through an independent runtime (no PPSSPP), with its own online backend.

## Status (tested state only)

| Area | State |
|---|---|
| Backend: guest auth, hashed tokens, expiry, rate limit, rooms, ready, matchmaking, forfeits, dual-report results, Elo | Implemented, 16 automated tests pass (`cd server && npm test`) |
| Native: ISO9660 reader, executable-type detection, ISO inspector tool | Implemented; also run once on a real Tekken 6 USA ISO (inspector only). Tests use synthetic images (`scripts/test-native.sh`) |
| Android app | Lobby prototype built by GitHub Actions (diagnostic screen, not the game). Never compiled in the authoring sandbox. Fixed debug signing key, remembers the server URL |
| Real-time lobby push (WebSocket) | NOT STARTED. Clients poll `GET /api/v1/state` |
| Password login, DB persistence | NOT STARTED. State is in memory; guests only |
| UDP gameplay transport, relay, rollback/delay sync | NOT STARTED |
| MIPS integer interpreter + single-precision FPU (delay slots, unaligned ops) + plain-ELF loader | Implemented, tested on hand-assembled programs. No PRX relocation, no FPU exception flags, no timing |
| VFPU (partial): vector loads/stores, vadd/vsub/vmul/vdiv, vdot/vhdp/vscl, vmov/vabs/vneg/vsat/vzero/vone/vrcp/vrsq/vsqrt/vsin/vcos, prefixes, viim | Implemented from memory of public documentation; tests prove internal consistency only. NO matrix ops, compares, conversions, vmin/vmax, vrot, vcst, control registers. Unsupported forms stop with BadInstruction |
| HLE kernel layer: NID derivation, syscall dispatch, core services (exit, display/vblank, controller, time, partition memory), thread scheduler (priorities, delay, sleep/wakeup), semaphores, event flags, mutexes | Implemented, tested with hand-assembled programs. Placeholder error codes, bump allocator, rough virtual clock, no timeslicing between equal-priority threads. No callbacks or module linker yet |
| GPU, audio, ad hoc net layer | NOT STARTED |
| Original Tekken 6 execution / online matches | NOT STARTED. Nothing here runs the game |
| Deployed internet service | NOT DEPLOYED. Dockerfile, `render.yaml`, proxy-aware rate limits and memory caps are written; see `docs/HOSTING.md`. Untested on a real host |

Not tested against a real ISO; all native tests use synthetic images.

## Run
    cd server && node src/main.ts     # Node 22.18+, no dependencies
    cd server && npm test
    ./scripts/test-native.sh          # needs g++ with C++20

## Server limits (honest)
- Combat runs on clients, so the server is not authoritative. It only accepts a result when both players agree; disagreement is recorded as disputed with no rating change. A colluding pair can still fake ranked results.
- Leaving or going silent mid-match forfeits to the opponent.
- State resets on restart; clients must re-authenticate.

## Inspect your own ISO (Termux or Linux)
    pkg install clang        # Termux only
    clang++ -std=c++20 -O2 -o t6_inspect native/tools/t6_inspect.cpp native/loader/iso9660.cpp
    ./t6_inspect /path/to/your/tekken6.iso
Prints file list, PARAM.SFO fields and whether the executable is ELF, PBP or encrypted `~PSP`. Metadata only; it copies nothing.
