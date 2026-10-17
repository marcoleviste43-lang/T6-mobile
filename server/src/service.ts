import { randomBytes, createHash, randomUUID } from "node:crypto";

export class ApiError extends Error {
  status: number; code: string;
  constructor(status: number, code: string, message: string) { super(message); this.status = status; this.code = code; }
}

export type Mode = "casual" | "ranked" | "private";
export type PlayerState = "IDLE" | "QUEUED" | "ROOM_JOINED" | "READY" | "LOADING" | "IN_MATCH" | "MATCH_FINISHED";
type RoomState = "WAITING" | "LOADING" | "IN_MATCH" | "FINISHED";

export interface Config {
  sessionTtlMs: number; queueTimeoutMs: number; readyTimeoutMs: number; disconnectMs: number; k: number;
  maxPlayers?: number;  // cap on live guest sessions (default 5000); keeps a public server's memory bounded
  maxMatches?: number;  // cap on stored match records (default 5000); oldest are dropped
}
export const defaultConfig: Config = {
  sessionTtlMs: 3_600_000, queueTimeoutMs: 120_000, readyTimeoutMs: 30_000, disconnectMs: 15_000, k: 32,
};

interface Player {
  id: string; name: string; rating: number; wins: number; losses: number;
  tokenHash: string; expiresAt: number; lastSeen: number;
  queue?: { mode: "casual" | "ranked"; since: number }; roomCode?: string;
}
interface Room {
  code: string; mode: Mode; players: string[]; host: string; ready: Set<string>; loaded: Set<string>;
  state: RoomState; matchId?: string; startedAt?: number; source: "private" | "matchmaking"; createdAt: number;
  reports: Map<string, string>;
}
export interface MatchRecord {
  matchId: string; mode: Mode; players: string[]; startedAt: number; endedAt: number;
  winnerId: string | null; reason: "agreed" | "disconnect" | "disputed"; ratingChanges?: Record<string, number>;
}

const CODE_ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
const NAME_RE = /^[A-Za-z0-9_\- ]{3,20}$/;
const hash = (t: string) => createHash("sha256").update(t).digest("hex");

export class Service {
  players = new Map<string, Player>();
  byToken = new Map<string, string>();
  rooms = new Map<string, Room>();
  matches: MatchRecord[] = [];
  cfg: Config;
  constructor(cfg: Config = defaultConfig) { this.cfg = cfg; }

  guest(name: unknown, now: number) {
    if (typeof name !== "string" || !NAME_RE.test(name)) throw new ApiError(400, "invalid_name", "displayName must be 3-20 chars of letters, digits, space, _ or -");
    if (this.players.size >= (this.cfg.maxPlayers ?? 5000)) throw new ApiError(503, "server_full", "server is full, try again later");
    const token = randomBytes(32).toString("base64url");
    const p: Player = { id: randomUUID(), name, rating: 1000, wins: 0, losses: 0, tokenHash: hash(token), expiresAt: now + this.cfg.sessionTtlMs, lastSeen: now };
    this.players.set(p.id, p); this.byToken.set(p.tokenHash, p.id);
    return { playerId: p.id, token, expiresAt: p.expiresAt };
  }

  authenticate(token: string | undefined, now: number): Player {
    if (!token) throw new ApiError(401, "unauthorized", "missing token");
    const id = this.byToken.get(hash(token));
    const p = id && this.players.get(id);
    if (!p) throw new ApiError(401, "unauthorized", "invalid or expired token");
    if (now >= p.expiresAt) { this.dropPlayer(p, now); throw new ApiError(401, "session_expired", "session expired"); }
    p.lastSeen = now;
    return p;
  }

  logout(p: Player, now: number) { this.dropPlayer(p, now); }

  private dropPlayer(p: Player, now: number) {
    this.cancelQueue(p);
    this.leave(p, now);
    this.byToken.delete(p.tokenHash); this.players.delete(p.id);
  }

  profile(p: Player) { return { playerId: p.id, displayName: p.name, rating: p.rating, wins: p.wins, losses: p.losses }; }

  stateOf(p: Player) {
    const room = p.roomCode ? this.rooms.get(p.roomCode) : undefined;
    let state: PlayerState = "IDLE";
    if (p.queue) state = "QUEUED";
    else if (room) {
      state = room.state === "WAITING" ? (room.ready.has(p.id) ? "READY" : "ROOM_JOINED") : room.state === "FINISHED" ? "MATCH_FINISHED" : room.state;
    }
    return {
      state,
      queue: p.queue ? { mode: p.queue.mode, since: p.queue.since } : undefined,
      room: room ? {
        code: room.code, mode: room.mode, host: room.host, state: room.state, matchId: room.matchId,
        players: room.players.map((id) => ({ playerId: id, displayName: this.players.get(id)?.name ?? "?", ready: room.ready.has(id) })),
      } : undefined,
    };
  }

  private requireIdle(p: Player) {
    if (p.queue || p.roomCode) throw new ApiError(409, "invalid_state", "leave your current queue or room first");
  }

  joinQueue(p: Player, mode: unknown, now: number) {
    if (mode !== "casual" && mode !== "ranked") throw new ApiError(400, "invalid_mode", "mode must be casual or ranked");
    this.requireIdle(p);
    p.queue = { mode, since: now };
    this.tryMatch(now);
    return this.stateOf(p);
  }
  cancelQueue(p: Player) { delete p.queue; }

  private tryMatch(now: number) {
    const waiting = [...this.players.values()].filter((x) => x.queue).sort((a, b) => a.queue!.since - b.queue!.since);
    for (const a of waiting) {
      if (!a.queue) continue;
      const b = waiting.find((c) => c !== a && c.queue && c.queue.mode === a.queue!.mode && this.compatible(a, c, now));
      if (!b) continue;
      const mode = a.queue.mode;
      delete a.queue; delete b.queue;
      this.makeRoom(mode, [a, b], "matchmaking", now);
    }
  }
  private compatible(a: Player, b: Player, now: number) {
    if (a.queue!.mode !== "ranked") return true;
    const waited = (now - Math.min(a.queue!.since, b.queue!.since)) / 1000;
    return Math.abs(a.rating - b.rating) <= 100 + 10 * waited;
  }

  private newCode() {
    for (;;) {
      const c = Array.from(randomBytes(6), (b) => CODE_ALPHABET[b % CODE_ALPHABET.length]).join("");
      if (!this.rooms.has(c)) return c;
    }
  }
  private makeRoom(mode: Mode, ps: Player[], source: Room["source"], now: number) {
    const room: Room = { code: this.newCode(), mode, players: ps.map((x) => x.id), host: ps[0].id, ready: new Set(), loaded: new Set(), state: "WAITING", source, createdAt: now, reports: new Map() };
    this.rooms.set(room.code, room);
    for (const x of ps) x.roomCode = room.code;
    return room;
  }
  createRoom(p: Player, now: number) {
    this.requireIdle(p);
    this.makeRoom("private", [p], "private", now);
    return this.stateOf(p);
  }
  joinRoom(p: Player, code: unknown, now: number) {
    if (typeof code !== "string" || !/^[A-Z0-9]{6}$/.test(code)) throw new ApiError(400, "invalid_code", "room code must be 6 characters");
    this.requireIdle(p);
    const room = this.rooms.get(code);
    if (!room || room.state !== "WAITING") throw new ApiError(404, "room_not_found", "no joinable room with that code");
    if (room.players.length >= 2) throw new ApiError(409, "room_full", "room is full");
    room.players.push(p.id); p.roomCode = code; room.createdAt = now;
    return this.stateOf(p);
  }
  setReady(p: Player, ready: unknown, now: number) {
    if (typeof ready !== "boolean") throw new ApiError(400, "invalid_body", "ready must be boolean");
    const room = this.roomOf(p);
    if (room.state !== "WAITING") throw new ApiError(409, "invalid_state", "match already started");
    ready ? room.ready.add(p.id) : room.ready.delete(p.id);
    if (room.players.length === 2 && room.ready.size === 2) {
      room.state = "LOADING"; room.matchId = randomUUID(); room.startedAt = now;
    }
    return this.stateOf(p);
  }
  matchLoaded(p: Player) {
    const room = this.roomOf(p);
    if (room.state !== "LOADING") throw new ApiError(409, "invalid_state", "room is not loading");
    room.loaded.add(p.id);
    if (room.loaded.size === 2) room.state = "IN_MATCH";
    return this.stateOf(p);
  }
  private roomOf(p: Player) {
    const room = p.roomCode ? this.rooms.get(p.roomCode) : undefined;
    if (!room) throw new ApiError(409, "invalid_state", "not in a room");
    return room;
  }
  leave(p: Player, now: number) {
    const room = p.roomCode ? this.rooms.get(p.roomCode) : undefined;
    delete p.roomCode;
    if (!room) return;
    const other = room.players.find((id) => id !== p.id);
    if ((room.state === "LOADING" || room.state === "IN_MATCH") && other) {
      this.finalize(room, other, "disconnect", now);
    }
    room.players = room.players.filter((id) => id !== p.id);
    room.ready.delete(p.id); room.loaded.delete(p.id);
    if (room.players.length === 0) { this.rooms.delete(room.code); return; }
    if (room.host === p.id) room.host = room.players[0];
    if (room.state === "WAITING") room.ready.clear();
  }

  reportResult(p: Player, matchId: unknown, winnerId: unknown, now: number) {
    const room = this.roomOf(p);
    if (typeof matchId !== "string" || matchId !== room.matchId) throw new ApiError(404, "match_not_found", "unknown match");
    if (room.state === "FINISHED") throw new ApiError(409, "already_finished", "match already finalized");
    if (room.state !== "IN_MATCH") throw new ApiError(409, "invalid_state", "match is not in progress");
    if (winnerId !== "draw" && !(typeof winnerId === "string" && room.players.includes(winnerId))) throw new ApiError(400, "invalid_winner", "winnerId must be a participant or 'draw'");
    room.reports.set(p.id, winnerId as string);
    if (room.reports.size === 2) {
      const [x, y] = [...room.reports.values()];
      if (x === y) this.finalize(room, x === "draw" ? null : x, "agreed", now);
      else this.finalize(room, null, "disputed", now);
    }
    return this.stateOf(p);
  }

  private finalize(room: Room, winnerId: string | null, reason: MatchRecord["reason"], now: number) {
    if (room.state === "FINISHED" || !room.matchId) return;
    room.state = "FINISHED";
    const rec: MatchRecord = { matchId: room.matchId, mode: room.mode, players: [...room.players], startedAt: room.startedAt!, endedAt: now, winnerId, reason };
    const [a, b] = room.players.map((id) => this.players.get(id));
    if (winnerId && a && b) {
      const w = winnerId === a.id ? a : b, l = w === a ? b : a;
      w.wins++; l.losses++;
      if (room.mode === "ranked" && reason !== "disputed") {
        const exp = 1 / (1 + 10 ** ((l.rating - w.rating) / 400));
        const d = Math.round(this.cfg.k * (1 - exp));
        w.rating += d; l.rating -= d;
        rec.ratingChanges = { [w.id]: d, [l.id]: -d };
      }
    }
    this.matches.push(rec);
    const cap = this.cfg.maxMatches ?? 5000;
    if (this.matches.length > cap) this.matches.splice(0, this.matches.length - cap);
  }

  history(p: Player) { return this.matches.filter((m) => m.players.includes(p.id)).slice(-50).reverse(); }

  sweep(now: number) {
    for (const p of [...this.players.values()]) {
      if (now >= p.expiresAt) { this.dropPlayer(p, now); continue; }
      if (p.queue && now - p.queue.since >= this.cfg.queueTimeoutMs) delete p.queue;
      const room = p.roomCode ? this.rooms.get(p.roomCode) : undefined;
      if (room && room.state !== "FINISHED" && now - p.lastSeen >= this.cfg.disconnectMs) this.leave(p, now);
    }
    for (const room of [...this.rooms.values()]) {
      if (room.state === "WAITING" && room.source === "matchmaking" && room.players.length === 2 && now - room.createdAt >= this.cfg.readyTimeoutMs) {
        for (const id of room.players) { const pl = this.players.get(id); if (pl) delete pl.roomCode; }
        this.rooms.delete(room.code);
      }
    }
    this.tryMatch(now);
  }
}
