import test from "node:test";
import assert from "node:assert/strict";
import type { AddressInfo } from "node:net";
import { Service } from "../src/service.ts";
import { createApp } from "../src/http.ts";

async function boot(opts = {}) {
  const srv = createApp(new Service(), opts);
  await new Promise<void>((r) => srv.listen(0, "127.0.0.1", r));
  const base = `http://127.0.0.1:${(srv.address() as AddressInfo).port}/api/v1`;
  const call = async (m: string, path: string, body?: unknown, token?: string) => {
    const r = await fetch(base + path, { method: m, headers: { "content-type": "application/json", ...(token ? { authorization: `Bearer ${token}` } : {}) }, body: body === undefined ? undefined : JSON.stringify(body) });
    return { status: r.status, json: (await r.json()) as any };
  };
  return { srv, call };
}

test("two independent HTTP clients: private room to MATCH_FINISHED", async () => {
  const { srv, call } = await boot();
  const health = await call("GET", "/health"); assert.equal(health.json.status, "ok");
  const a = (await call("POST", "/auth/guest", { displayName: "alice" })).json;
  const b = (await call("POST", "/auth/guest", { displayName: "bobby" })).json;
  const room = (await call("POST", "/rooms", {}, a.token)).json;
  assert.equal((await call("POST", "/rooms/join", { code: room.room.code }, b.token)).json.state, "ROOM_JOINED");
  await call("POST", "/rooms/ready", { ready: true }, a.token);
  const st = (await call("POST", "/rooms/ready", { ready: true }, b.token)).json;
  assert.equal(st.state, "LOADING");
  await call("POST", "/matches/loaded", {}, a.token); await call("POST", "/matches/loaded", {}, b.token);
  const matchId = st.room.matchId;
  await call("POST", "/matches/result", { matchId, winnerId: b.playerId }, a.token);
  const fin = (await call("POST", "/matches/result", { matchId, winnerId: b.playerId }, b.token)).json;
  assert.equal(fin.state, "MATCH_FINISHED");
  const hist = (await call("GET", "/matches/history", undefined, a.token)).json;
  assert.equal(hist[0].winnerId, b.playerId);
  srv.close();
});

test("authorization, malformed input, oversized body", async () => {
  const { srv, call } = await boot();
  assert.equal((await call("GET", "/profile")).status, 401);
  assert.equal((await call("GET", "/profile", undefined, "garbage")).status, 401);
  assert.equal((await call("POST", "/auth/guest", { displayName: 5 })).status, 400);
  const raw = await fetch(srv.address() ? `http://127.0.0.1:${(srv.address() as AddressInfo).port}/api/v1/auth/guest` : "", { method: "POST", body: "{not json" });
  assert.equal(raw.status, 400);
  assert.equal((await call("GET", "/nope")).status, 401);
  srv.close();
});

test("rate limiting returns 429", async () => {
  const { srv, call } = await boot({ rateCapacity: 3, ratePerSec: 0.001 });
  const codes: number[] = [];
  for (let i = 0; i < 6; i++) codes.push((await call("GET", "/health")).status);
  assert.deepEqual(codes.slice(0, 3), [200, 200, 200]); assert.ok(codes.includes(429));
  srv.close();
});

test("rate limit is per client behind a proxy, and a forged left-most X-Forwarded-For does not help", async () => {
  const { srv, call: _ } = await boot({ rateCapacity: 2, ratePerSec: 0, trustProxyHops: 1 });
  const base = `http://127.0.0.1:${(srv.address() as AddressInfo).port}/api/v1/health`;
  const hit = async (xff: string) => (await fetch(base, { headers: { "x-forwarded-for": xff } })).status;
  assert.equal(await hit("1.1.1.1"), 200); assert.equal(await hit("1.1.1.1"), 200);
  assert.equal(await hit("1.1.1.1"), 429);                  // same client is limited
  assert.equal(await hit("2.2.2.2"), 200);                  // a different client is not
  assert.equal(await hit("9.9.9.9, 1.1.1.1"), 429);         // forged left entry; the proxy-appended right entry decides
  srv.close();
  const plain = await boot({ rateCapacity: 2, ratePerSec: 0 });  // header ignored when no proxy is trusted
  const b2 = `http://127.0.0.1:${(plain.srv.address() as AddressInfo).port}/api/v1/health`;
  const hit2 = async (xff: string) => (await fetch(b2, { headers: { "x-forwarded-for": xff } })).status;
  assert.equal(await hit2("1.1.1.1"), 200); assert.equal(await hit2("2.2.2.2"), 200); assert.equal(await hit2("3.3.3.3"), 429);
  plain.srv.close();
});

test("server_full cap and match history cap keep memory bounded", () => {
  const svc = new Service({ sessionTtlMs: 3_600_000, queueTimeoutMs: 120_000, readyTimeoutMs: 30_000, disconnectMs: 15_000, k: 32, maxPlayers: 2, maxMatches: 3 });
  const ga = svc.guest("alice", 0), gb = svc.guest("bobby", 0);
  assert.throws(() => svc.guest("carol", 0), (e: any) => e.status === 503 && e.code === "server_full");
  const a = svc.authenticate(ga.token, 1), b = svc.authenticate(gb.token, 1);
  for (let i = 0; i < 5; i++) {  // play five real matches; only the newest three records are kept
    const code = (svc.createRoom(a, 1) as any).room.code;
    svc.joinRoom(b, code, 1); svc.setReady(a, true, 1); svc.setReady(b, true, 1);
    svc.matchLoaded(a); svc.matchLoaded(b);
    const matchId = (svc.stateOf(a) as any).room.matchId;
    svc.reportResult(a, matchId, a.id, 1); svc.reportResult(b, matchId, a.id, 1);
    svc.leave(a, 1); svc.leave(b, 1);
  }
  assert.equal(svc.matches.length, 3);
});
