# API v1 (all JSON, `Authorization: Bearer <token>` except health/guest)
POST /auth/guest {displayName} -> {playerId, token, expiresAt}
POST /auth/logout | GET /profile | GET /state | GET /health
POST /matchmaking/join {mode: casual|ranked} | POST /matchmaking/cancel
POST /rooms | POST /rooms/join {code} | POST /rooms/ready {ready} | POST /rooms/leave
POST /matches/loaded | POST /matches/result {matchId, winnerId|"draw"} | GET /matches/history
Errors: {error:{code,message}}; 400 validation, 401 auth, 404, 409 invalid state, 413, 429.
Player states: IDLE QUEUED ROOM_JOINED READY LOADING IN_MATCH MATCH_FINISHED.
GET /state?since=<version>&wait=<ms, max 10000>: long poll. Every state response carries `version`. If `since` is empty or differs from the current version it answers at once; otherwise it waits up to `wait` ms for a change and then answers with the same version. A waiting client counts as connected (no forfeit).
