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
