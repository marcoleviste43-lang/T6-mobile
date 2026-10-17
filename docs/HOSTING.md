# Hosting the backend so two phones can play each other

Nothing here has been deployed or tested against a real host: the sandbox this was written in has no internet and no Docker. The server itself is tested (`cd server && npm test`). The host-specific steps below are from memory of each service's docs, so expect small differences in their menus.

## What changed in the server for public use
- `TRUST_PROXY_HOPS=1` makes rate limits per client instead of per proxy. The client address is taken from the right-hand end of `X-Forwarded-For` (the part your own proxy added), so a forged left-hand entry does not help. Default 0 ignores the header, which is right when nothing sits in front of the server.
- Memory is bounded: at most 5000 live guest sessions (the 5001st gets `503 server_full`) and 5000 stored match records.
- SIGTERM closes the server cleanly, as hosts expect.
- `PORT` is read from the environment (hosts set it), default 8080.

## Option A: Render (works from a phone browser, free plan)
1. Push this repo to GitHub (you already do).
2. On render.com: New, then Blueprint, pick the repo. It reads `render.yaml` (Docker build of `server/`, health check `/api/v1/health`, `TRUST_PROXY_HOPS=1`).
3. When it is live, open `https://YOUR-NAME.onrender.com/api/v1/health` in the phone browser. You should see `{"status":"ok",...}`.
4. In the app, set the server URL to `https://YOUR-NAME.onrender.com` and tap Health check.

Free-plan caveats: the service sleeps after about 15 minutes idle and takes up to a minute to wake, and all state (guests, rooms, history) resets on every restart or sleep. Fine for testing, not for real use.

## Option B: your own phone through a tunnel (no account, no deploy)
The backend already runs in Termux. A tunnel gives it a public https address:

    pkg install cloudflared
    cd ~/t6/server
    TRUST_PROXY_HOPS=1 node src/main.ts        # leave running
    # second Termux session:
    cloudflared tunnel --url http://localhost:8080

It prints a `https://something.trycloudflare.com` address. Put that in both phones' apps. The address changes every time you restart the tunnel, and the server only lives while your phone keeps Termux running (hold a wakelock). I have not run cloudflared on Termux myself; if the package is missing, tell me and we will pick another tunnel.

## Honest limits of a public server
- Guest-only login, no passwords, no database. Anyone who finds the URL can create guest sessions (rate-limited and capped).
- The server is not authoritative about who won; see the README. A colluding pair can fake ranked results.
- Traffic is as private as the URL scheme: use https (Render and the tunnel both provide it). Plain `http://` to a public host sends tokens in the clear.
- The app now long-polls the state (up to 8 seconds per request), so lobby changes arrive quickly and idle traffic is low. WebSocket push is not built.
