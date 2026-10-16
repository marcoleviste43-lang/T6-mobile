# Compatibility and blockers

Evidence level: none yet from a real ISO.

1. EBOOT decryption (expected, unverified). Retail PSP executables are normally `~PSP`-encrypted. The ISO reader detects this but cannot decrypt. A clean-room KIRK/PRX decryption path is required before any CPU work can run the game. This is the first gate for Phase 5 and needs a developer-supplied ISO to confirm.
2. Ad hoc networking. Which sceNetAdhoc* calls Tekken 6 uses, and its packet timing, are unknown until traced. Do not design the transport around guesses.
3. Rollback feasibility depends on snapshotting full guest state (CPU, RAM, GE, audio). Unknown; delay-based sync is the fallback.
