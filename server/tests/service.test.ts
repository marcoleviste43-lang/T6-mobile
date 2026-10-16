import test from "node:test";
import assert from "node:assert/strict";
import { Service, ApiError } from "../src/service.ts";

const mk = () => new Service({ sessionTtlMs: 10_000, queueTimeoutMs: 5_000, readyTimeoutMs: 3_000, disconnectMs: 2_000, k: 32 });
const two = (s: Service, t = 0) => { const a = s.guest("alice", t), b = s.guest("bobby", t); return [s.authenticate(a.token, t), s.authenticate(b.token, t), a, b] as const; };
const throwsCode = (fn: () => unknown, code: string) => assert.throws(fn, (e: any) => e instanceof ApiError && e.code === code);

test("guest auth validates names and issues unique tokens", () => {
  const s = mk();
  throwsCode(() => s.guest("x", 0), "invalid_name");
  throwsCode(() => s.guest("<script>", 0), "invalid_name");
  assert.notEqual(s.guest("alice", 0).token, s.guest("alice", 0).token);
});

test("tokens are stored hashed and expire", () => {
  const s = mk(); const g = s.guest("alice", 0);
  assert.ok(![...s.byToken.keys()].includes(g.token));
  s.authenticate(g.token, 9_999);
  throwsCode(() => s.authenticate(g.token, 10_000), "session_expired");
  throwsCode(() => s.authenticate(g.token, 10_001), "unauthorized");
  throwsCode(() => s.authenticate(undefined, 0), "unauthorized");
});

test("private room: create, join, ready, load -> IN_MATCH", () => {
  const s = mk(); const [a, b] = two(s);
  const code = s.createRoom(a, 0).room!.code;
  throwsCode(() => s.createRoom(a, 0), "invalid_state");
  assert.equal(s.joinRoom(b, code, 1).state, "ROOM_JOINED");
  const c = s.authenticate(s.guest("carol", 0).token, 0);
  throwsCode(() => s.joinRoom(c, code, 1), "room_full");
  throwsCode(() => s.joinRoom(c, "ZZZZZZ", 1), "room_not_found");
  assert.equal(s.setReady(a, true, 2).state, "READY");
  assert.equal(s.setReady(b, true, 2).state, "LOADING");
  s.matchLoaded(a); assert.equal(s.matchLoaded(b).state, "IN_MATCH");
});

test("casual matchmaking pairs two players; cancel works; double-queue rejected", () => {
  const s = mk(); const [a, b] = two(s);
  assert.equal(s.joinQueue(a, "casual", 0).state, "QUEUED");
  throwsCode(() => s.joinQueue(a, "casual", 0), "invalid_state");
  s.cancelQueue(a); assert.equal(s.stateOf(a).state, "IDLE");
  s.joinQueue(a, "casual", 0);
  assert.equal(s.joinQueue(b, "casual", 1).state, "ROOM_JOINED");
  assert.equal(s.stateOf(a).room!.code, s.stateOf(b).room!.code);
  throwsCode(() => s.joinQueue(a, "bogus", 0), "invalid_mode");
});

test("ranked pairing respects rating window, widens over time", () => {
  const s = new Service({ sessionTtlMs: 1e9, queueTimeoutMs: 1e9, readyTimeoutMs: 1e9, disconnectMs: 1e9, k: 32 });
  const [a, b] = two(s); b.rating = 1400;
  s.joinQueue(a, "ranked", 0); s.joinQueue(b, "ranked", 0);
  assert.equal(s.stateOf(a).state, "QUEUED");
  s.sweep(10_000); // window 100 + 100 = 200 < 400
  assert.equal(s.stateOf(a).state, "QUEUED");
  s.sweep(31_000); // window 100 + 310 >= 400
  assert.equal(s.stateOf(a).state, "ROOM_JOINED");
  assert.equal(s.stateOf(b).state, "ROOM_JOINED");
});

test("queue timeout removes player", () => {
  const s = mk(); const [a] = two(s);
  s.joinQueue(a, "ranked", 0); a.lastSeen = 5_000; s.sweep(5_000);
  assert.equal(s.stateOf(a).state, "IDLE");
});

test("matchmaking room dissolves if nobody readies in time", () => {
  const s = mk(); const [a, b] = two(s);
  s.joinQueue(a, "casual", 0); s.joinQueue(b, "casual", 0);
  a.lastSeen = b.lastSeen = 3_000; s.sweep(3_000);
  assert.equal(s.stateOf(a).state, "IDLE"); assert.equal(s.stateOf(b).state, "IDLE"); assert.equal(s.rooms.size, 0);
});

function startMatch(s: Service, mode: "ranked" | "private" = "private") {
  const [a, b] = two(s);
  if (mode === "ranked") { s.joinQueue(a, "ranked", 0); s.joinQueue(b, "ranked", 0); }
  else { const code = s.createRoom(a, 0).room!.code; s.joinRoom(b, code, 0); }
  s.setReady(a, true, 1); const st = s.setReady(b, true, 1);
  s.matchLoaded(a); s.matchLoaded(b);
  return { a, b, matchId: st.room!.matchId! };
}

test("agreed result updates ranked ratings and history", () => {
  const s = mk(); const { a, b, matchId } = startMatch(s, "ranked");
  s.reportResult(a, matchId, a.id, 10); s.reportResult(b, matchId, a.id, 11);
  assert.equal(a.rating, 1016); assert.equal(b.rating, 984);
  assert.equal(s.history(a)[0].reason, "agreed");
  assert.equal(s.stateOf(a).state, "MATCH_FINISHED");
  throwsCode(() => s.reportResult(a, matchId, a.id, 12), "already_finished");
});

test("conflicting reports are disputed: no rating change", () => {
  const s = mk(); const { a, b, matchId } = startMatch(s, "ranked");
  s.reportResult(a, matchId, a.id, 10); s.reportResult(b, matchId, b.id, 10);
  assert.equal(a.rating, 1000); assert.equal(s.history(a)[0].reason, "disputed");
});

test("result validation: bad winner, wrong match, outsider", () => {
  const s = mk(); const { a, matchId } = startMatch(s);
  throwsCode(() => s.reportResult(a, matchId, "someone-else", 5), "invalid_winner");
  throwsCode(() => s.reportResult(a, "nope", a.id, 5), "match_not_found");
  const c = s.authenticate(s.guest("carol", 0).token, 0);
  throwsCode(() => s.reportResult(c, matchId, a.id, 5), "invalid_state");
});

test("leaving mid-match forfeits to opponent; stale peer is swept as disconnect", () => {
  const s = mk(); const { a, b } = startMatch(s, "ranked");
  b.lastSeen = 5_000; a.lastSeen = 0; s.sweep(5_000); // a silent >2s
  const rec = s.history(b)[0];
  assert.equal(rec.reason, "disconnect"); assert.equal(rec.winnerId, b.id);
  assert.equal(b.rating, 1016);
  assert.equal(s.stateOf(b).state, "MATCH_FINISHED");
  s.leave(b, 5_001); assert.equal(s.rooms.size, 0);
});

test("host leaving a waiting room promotes the other player", () => {
  const s = mk(); const [a, b] = two(s);
  const code = s.createRoom(a, 0).room!.code; s.joinRoom(b, code, 0); s.setReady(b, true, 0);
  s.leave(a, 1);
  const st = s.stateOf(b); assert.equal(st.room!.host, b.id); assert.equal(st.state, "ROOM_JOINED");
});

test("logout drops player from queue and room", () => {
  const s = mk(); const [a, b] = two(s);
  const code = s.createRoom(a, 0).room!.code; s.joinRoom(b, code, 0);
  s.logout(a, 1); assert.equal(s.stateOf(b).room!.players.length, 1);
});
