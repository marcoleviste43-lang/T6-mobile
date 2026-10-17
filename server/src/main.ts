import { Service } from "./service.ts";
import { createApp } from "./http.ts";
const svc = new Service();
const port = Number(process.env.PORT ?? 8080);
// Set TRUST_PROXY_HOPS=1 when running behind one reverse proxy or tunnel, so rate limits are per client, not per proxy.
const trustProxyHops = Math.max(0, Math.floor(Number(process.env.TRUST_PROXY_HOPS ?? 0)) || 0);
setInterval(() => svc.sweep(Date.now()), 1000).unref();
const server = createApp(svc, { trustProxyHops }).listen(port, () => console.log(JSON.stringify({ level: "info", msg: "listening", port, trustProxyHops })));
// Hosts stop containers with SIGTERM; close cleanly instead of dropping connections.
for (const sig of ["SIGTERM", "SIGINT"] as const) process.on(sig, () => { console.log(JSON.stringify({ level: "info", msg: "shutting down", sig })); server.close(() => process.exit(0)); setTimeout(() => process.exit(0), 3000).unref(); });
