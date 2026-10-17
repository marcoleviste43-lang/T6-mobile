import http from "node:http";
import { createHash } from "node:crypto";
import { ApiError, Service } from "./service.ts";

export class RateLimiter {
  private b = new Map<string, { tokens: number; at: number }>();
  private capacity: number; private perSec: number;
  constructor(capacity: number, perSec: number) { this.capacity = capacity; this.perSec = perSec; }
  allow(key: string, now: number) {
    const e = this.b.get(key) ?? { tokens: this.capacity, at: now };
    e.tokens = Math.min(this.capacity, e.tokens + ((now - e.at) / 1000) * this.perSec); e.at = now;
    const ok = e.tokens >= 1; if (ok) e.tokens -= 1;
    this.b.set(key, e); return ok;
  }
}

const stateVersion = (v: unknown) => createHash("sha256").update(JSON.stringify(v)).digest("hex").slice(0, 12);

const MAX_BODY = 4096;
function readBody(req: http.IncomingMessage): Promise<any> {
  return new Promise((resolve, reject) => {
    let size = 0; const chunks: Buffer[] = [];
    req.on("data", (c: Buffer) => { size += c.length; if (size > MAX_BODY) { reject(new ApiError(413, "too_large", "body too large")); req.destroy(); } else chunks.push(c); });
    req.on("end", () => {
      if (size === 0) return resolve({});
      try { const v = JSON.parse(Buffer.concat(chunks).toString("utf8")); resolve(v && typeof v === "object" ? v : {}); }
      catch { reject(new ApiError(400, "invalid_json", "body is not valid JSON")); }
    });
    req.on("error", reject);
  });
}

// Rate-limit key. Behind a reverse proxy every request arrives from the proxy's address, so with
// trustProxyHops = N the client is the Nth entry from the RIGHT of X-Forwarded-For (the part our own
// proxies appended; anything further left can be forged by the client). 0 = ignore the header.
export function clientKey(remote: string | undefined, xff: string | string[] | undefined, hops: number): string {
  if (hops > 0 && xff) {
    const parts = (Array.isArray(xff) ? xff.join(",") : xff).split(",").map((x) => x.trim()).filter(Boolean);
    if (parts.length >= hops) return parts[parts.length - hops];
  }
  return remote ?? "?";
}

export function createApp(svc: Service, opts: { now?: () => number; rateCapacity?: number; ratePerSec?: number; trustProxyHops?: number } = {}) {
  const now = opts.now ?? Date.now;
  const limiter = new RateLimiter(opts.rateCapacity ?? 30, opts.ratePerSec ?? 10);
  return http.createServer(async (req, res) => {
    const send = (status: number, body: unknown) => { res.writeHead(status, { "content-type": "application/json" }); res.end(JSON.stringify(body)); };
    const sleep = (ms: number) => new Promise<void>((r) => setTimeout(r, ms));
    try {
      const t = now();
      if (!limiter.allow(clientKey(req.socket.remoteAddress, req.headers["x-forwarded-for"], opts.trustProxyHops ?? 0), t)) throw new ApiError(429, "rate_limited", "too many requests");
      const url = new URL(req.url ?? "/", "http://x");
      const route = `${req.method} ${url.pathname}`;
      if (route === "GET /api/v1/health") return send(200, { status: "ok", players: svc.players.size, rooms: svc.rooms.size });
      const body = req.method === "POST" ? await readBody(req) : {};
      if (route === "POST /api/v1/auth/guest") return send(200, svc.guest(body.displayName, t));
      const auth = req.headers.authorization;
      const token = auth?.startsWith("Bearer ") ? auth.slice(7) : undefined;
      const p = svc.authenticate(token, t);
      switch (route) {
        case "POST /api/v1/auth/logout": svc.logout(p, t); return send(200, { ok: true });
        case "GET /api/v1/profile": return send(200, svc.profile(p));
        case "GET /api/v1/state": {
          // Long poll: ?since=<version>&wait=<ms, max 10000>. Returns at once if the state differs from `since`,
          // otherwise waits up to `wait` ms for a change. Re-authenticating each tick keeps the player marked as
          // connected, so a waiting client is not forfeited by the disconnect timer.
          const since = url.searchParams.get("since") ?? "";
          const waitMs = Math.min(10_000, Math.max(0, Number(url.searchParams.get("wait") ?? 0) || 0));
          const started = Date.now();
          let cur = p, view = svc.stateOf(cur), version = stateVersion(view);
          while (since !== "" && version === since && Date.now() - started < waitMs && !req.socket.destroyed) {
            await sleep(200);
            cur = svc.authenticate(token, now());
            view = svc.stateOf(cur); version = stateVersion(view);
          }
          return send(200, { ...view, version });
        }
        case "POST /api/v1/matchmaking/join": return send(200, svc.joinQueue(p, body.mode, t));
        case "POST /api/v1/matchmaking/cancel": svc.cancelQueue(p); return send(200, svc.stateOf(p));
        case "POST /api/v1/rooms": return send(200, svc.createRoom(p, t));
        case "POST /api/v1/rooms/join": return send(200, svc.joinRoom(p, body.code, t));
        case "POST /api/v1/rooms/ready": return send(200, svc.setReady(p, body.ready, t));
        case "POST /api/v1/rooms/leave": svc.leave(p, t); return send(200, svc.stateOf(p));
        case "POST /api/v1/matches/loaded": return send(200, svc.matchLoaded(p));
        case "POST /api/v1/matches/result": return send(200, svc.reportResult(p, body.matchId, body.winnerId, t));
        case "GET /api/v1/matches/history": return send(200, svc.history(p));
      }
      throw new ApiError(404, "not_found", "unknown route");
    } catch (e) {
      if (e instanceof ApiError) return send(e.status, { error: { code: e.code, message: e.message } });
      console.error(JSON.stringify({ level: "error", msg: String(e) }));
      send(500, { error: { code: "internal", message: "internal error" } });
    }
  });
}
