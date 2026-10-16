import { Service } from "./service.ts";
import { createApp } from "./http.ts";
const svc = new Service();
const port = Number(process.env.PORT ?? 8080);
setInterval(() => svc.sweep(Date.now()), 1000).unref();
createApp(svc).listen(port, () => console.log(JSON.stringify({ level: "info", msg: "listening", port })));
