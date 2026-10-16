# API v1 (all JSON, `Authorization: Bearer <token>` except health/guest)
POST /auth/guest {displayName} -> {playerId, token, expiresAt}
POST /auth/logout | GET /profile | GET /state | GET /health
POST /matchmaking/join {mode: casual|ranked} | POST /matchmaking/cancel
POST /rooms | POST /rooms/join {code} | POST /rooms/ready {ready} | POST /rooms/leave
POST /matches/loaded | POST /matches/result {matchId, winnerId|"draw"} | GET /matches/history
Errors: {error:{code,message}}; 400 validation, 401 auth, 404, 409 invalid state, 413, 429.
Player states: IDLE QUEUED ROOM_JOINED READY LOADING IN_MATCH MATCH_FINISHED.
