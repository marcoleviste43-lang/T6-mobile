# TEKKEN 6 NATIVE ONLINE

Multiplayer-only Android app that is meant to run the user's own PSP Tekken 6 ISO through an independent runtime (no PPSSPP), with its own online backend.

## Status (tested state only)

| Area | State |
|---|---|
| Backend: guest auth, hashed tokens, expiry, rate limit, rooms, ready, matchmaking, forfeits, dual-report results, Elo | Implemented, 16 automated tests pass (`cd server && npm test`) |
| Native: ISO9660 reader, executable-type detection, ISO inspector tool | Implemented; also run once on a real Tekken 6 USA ISO (inspector only). Tests use synthetic images (`scripts/test-native.sh`) |
| Android app | NOT STARTED. No Android SDK/NDK/Gradle in this environment |
| Real-time lobby push (WebSocket) | NOT STARTED. Clients poll `GET /api/v1/state` |
| Password login, DB persistence | NOT STARTED. State is in memory; guests only |
| UDP gameplay transport, relay, rollback/delay sync | NOT STARTED |
| MIPS integer interpreter + single-precision FPU (delay slots, unaligned ops) + plain-ELF loader | Implemented, tested on hand-assembled programs. No VFPU, no PRX relocation, no FPU exception flags, no timing |
| Kernel HLE, GPU, audio, ad hoc net layer | NOT STARTED |
| Original Tekken 6 execution / online matches | NOT STARTED. Nothing here runs the game |
| Deployed internet service | NO. Local only |

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
