import assert from "node:assert/strict";
import { test } from "node:test";

import { ApiError, createApi } from "../lib/api.js";

function fakeFetch(handler) {
  const calls = [];
  const fetch = async (path, init) => {
    calls.push({ path, init });
    const { status = 200, body = "" } = handler(path, init);
    return { ok: status >= 200 && status < 300, status, text: async () => (typeof body === "string" ? body : JSON.stringify(body)) };
  };
  return { fetch, calls };
}

test("GET returns parsed JSON and sends no body", async () => {
  const { fetch, calls } = fakeFetch(() => ({ body: { version: "0.1.0" } }));
  const api = createApi(fetch);
  assert.deepEqual(await api.get("/api/status"), { version: "0.1.0" });
  assert.equal(calls[0].init.method, "GET");
  assert.equal(calls[0].init.body, undefined);
});

test("requests that change state always send JSON, even without a body", async () => {
  const { fetch, calls } = fakeFetch(() => ({ status: 202, body: { ok: true } }));
  const api = createApi(fetch);
  await api.post("/api/device/restart");
  await api.patch("/api/config", { mqtt: { port: 8883 } });
  await api.del("/api/session");
  for (const call of calls) {
    assert.equal(call.init.headers["Content-Type"], "application/json");
  }
  assert.equal(calls[0].init.body, "{}");
  assert.equal(calls[1].init.body, '{"mqtt":{"port":8883}}');
  assert.equal(calls[2].init.method, "DELETE");
});

test("API errors carry the status, code, message and field details", async () => {
  const { fetch } = fakeFetch(() => ({
    status: 422,
    body: { error: { code: "invalid_config", message: "Some settings are not valid.", details: [{ path: "mqtt.port", message: "use a port" }] } },
  }));
  const api = createApi(fetch);
  await assert.rejects(api.patch("/api/config", {}), (error) => {
    assert.ok(error instanceof ApiError);
    assert.equal(error.status, 422);
    assert.equal(error.code, "invalid_config");
    assert.equal(error.message, "Some settings are not valid.");
    assert.equal(error.details[0].path, "mqtt.port");
    return true;
  });
});

test("errors without a JSON body still have a code", async () => {
  const { fetch } = fakeFetch(() => ({ status: 500, body: "boom" }));
  await assert.rejects(createApi(fetch).get("/api/status"), (error) => error.code === "http_500" && error.status === 500);
});

test("a box that does not answer is a network error", async () => {
  const api = createApi(async () => {
    throw new TypeError("Failed to fetch");
  });
  await assert.rejects(api.get("/api/status"), (error) => error.status === 0 && error.code === "network");
});

test("an empty success body is null", async () => {
  const { fetch } = fakeFetch(() => ({ status: 204, body: "" }));
  assert.equal(await createApi(fetch).post("/api/feedback", { result: "ok" }), null);
});
