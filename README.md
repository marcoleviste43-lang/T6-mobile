# TEKKEN 6 NATIVE ONLINE

Multiplayer-only Android app that is meant to run the user's own PSP Tekken 6 ISO through an independent runtime (no PPSSPP), with its own online backend.

## Status (tested state only)

| Area | State |
|---|---|
| Backend: guest auth, hashed tokens, expiry, rate limit, rooms, ready, matchmaking, forfeits, dual-report results, Elo | Implemented, 16 automated tests pass (`cd server && npm test`) |
| Native: ISO9660 reader, executable-type detection | Implemented, 6 checks pass under ASan/UBSan on synthetic images (`scripts/test-native.sh`) |
| Android app | NOT STARTED. No Android SDK/NDK/Gradle in this environment |
| Real-time lobby push (WebSocket) | NOT STARTED. Clients poll `GET /api/v1/state` |
| Password login, DB persistence | NOT STARTED. State is in memory; guests only |
| UDP gameplay transport, relay, rollback/delay sync | NOT STARTED |
| PSP CPU, kernel HLE, GPU, audio, ad hoc net layer | NOT STARTED |
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
