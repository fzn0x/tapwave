import assert from "node:assert/strict";
import { test } from "node:test";

import * as fmt from "../lib/format.js";

const NOW = Date.parse("2026-10-15T08:30:00Z");
const ago = (seconds) => new Date(NOW - seconds * 1000).toISOString();

test("relative times read like a person", () => {
  assert.equal(fmt.relativeTime(ago(3), NOW), "Just now");
  assert.equal(fmt.relativeTime(ago(42), NOW), "42 seconds ago");
  assert.equal(fmt.relativeTime(ago(61), NOW), "1 minute ago");
  assert.equal(fmt.relativeTime(ago(60 * 5), NOW), "5 minutes ago");
  assert.equal(fmt.relativeTime(ago(3600), NOW), "1 hour ago");
  assert.equal(fmt.relativeTime(ago(3600 * 7), NOW), "7 hours ago");
  assert.equal(fmt.relativeTime(ago(3600 * 30), NOW), "Yesterday");
  assert.equal(fmt.relativeTime(ago(86400 * 4), NOW), "4 days ago");
  assert.equal(fmt.relativeTime("not a time", NOW), "");
  assert.equal(fmt.relativeTime(new Date(NOW + 5000).toISOString(), NOW), "Just now");
});

test("durations and sizes", () => {
  assert.equal(fmt.duration(59), "0 minutes");
  assert.equal(fmt.duration(3600 * 5 + 720), "5 hours 12 minutes");
  assert.equal(fmt.duration(86400 * 2 + 3600 * 3), "2 days 3 hours");
  assert.equal(fmt.bytes(512), "512 B");
  assert.equal(fmt.bytes(143360), "140 KB");
  assert.equal(fmt.bytes(4 * 1024 * 1024), "4.0 MB");
  assert.equal(fmt.bytes(undefined), "");
});

test("Wi-Fi signal in words and bars", () => {
  assert.deepEqual(fmt.signal(-48), { bars: 4, label: "excellent signal" });
  assert.deepEqual(fmt.signal(-60), { bars: 3, label: "good signal" });
  assert.deepEqual(fmt.signal(-72), { bars: 2, label: "fair signal" });
  assert.deepEqual(fmt.signal(-85), { bars: 1, label: "weak signal" });
  assert.deepEqual(fmt.signal(0), { bars: 0, label: "" });
  assert.deepEqual(fmt.signal(undefined), { bars: 0, label: "" });
});

test("a Home Assistant tag uses its text as the title", () => {
  const d = fmt.describeScan({ kind: "ha_tag", id: "abc", text: "Bedtime lights" });
  assert.equal(d.title, "Bedtime lights");
  assert.equal(d.kind, "Home Assistant tag");
  assert.match(d.meaning, /Home Assistant runs/);
  assert.equal(d.problem, "");
});

test("a plain card is named by its kind", () => {
  const d = fmt.describeScan({ kind: "uid", id: "04-A2" });
  assert.equal(d.title, "Plain card");
  assert.match(d.meaning, /Nothing is written/);
});

test("tapwave actions say what they ask for, and that playing comes later", () => {
  const sd = fmt.describeScan({ kind: "tapwave", action: { name: "sd", target: "Music", supported: true } });
  assert.equal(sd.meaning, "Asks the box to play “Music” from the SD card. Playing on the box arrives in a later update.");
  const ha = fmt.describeScan({ kind: "tapwave", action: { name: "ha", target: "spotify://album/x", supported: true } });
  assert.equal(ha.meaning, "Asks the box to ask Home Assistant to play spotify://album/x.");
  const unknown = fmt.describeScan({ kind: "tapwave", action: { name: "lights", target: "x", supported: false } });
  assert.match(unknown.meaning, /does not know yet/);
  const broken = fmt.describeScan({ kind: "tapwave", action: { supported: false, error: "a '%' is not followed by two hex digits" } });
  assert.match(broken.meaning, /damaged: a '%'/);
});

test("links, music and text", () => {
  assert.equal(fmt.describeScan({ kind: "url", uri: "https://x.example" }).meaning, "Opens https://x.example");
  assert.match(fmt.describeScan({ kind: "music" }).meaning, /Music Assistant/);
  assert.equal(fmt.describeScan({ kind: "text", text: "Hi" }).meaning, "Carries the text “Hi”.");
});

test("a read problem is reported but the ID still works", () => {
  const d = fmt.describeScan({ kind: "uid", ndef_error: "the tag is not formatted for NDEF" });
  assert.equal(d.problem, "The data on this card could not be read: the tag is not formatted for NDEF. Its ID still works.");
});

test("records become a label and a value", () => {
  assert.deepEqual(fmt.describeRecord({ uri: "https://a" }), { label: "Address", value: "https://a" });
  assert.deepEqual(fmt.describeRecord({ text: "Hi", lang: "de" }), { label: "Text (de)", value: "Hi" });
  assert.deepEqual(fmt.describeRecord({ type: "application/json", payload: "7B7D", payload_text: "{}" }), {
    label: "application/json",
    value: "{}",
  });
  assert.deepEqual(fmt.describeRecord({ type: "x", payload: "00FF" }), { label: "x", value: "00FF" });
});

test("field problems are grouped by path", () => {
  const grouped = fmt.problemsByPath([
    { path: "mqtt.port", message: "use a port from 1 to 65535" },
    { path: "pins.led", message: "strapping pin", warning: true },
    { path: "pins.led", message: "already used" },
    { message: "no path" },
  ]);
  assert.deepEqual(Object.keys(grouped), ["mqtt.port", "pins.led"]);
  assert.equal(grouped["pins.led"].length, 2);
  assert.equal(grouped["pins.led"][0].warning, true);
});

test("only changed fields are sent, and redacted secrets are never sent back", () => {
  const original = { "mqtt.host": "a", "mqtt.port": 1883, "mqtt.password": "********" };
  assert.deepEqual(fmt.changedFields(original, { ...original }), {});
  assert.deepEqual(fmt.changedFields(original, { ...original, "mqtt.port": 8883 }), { "mqtt.port": 8883 });
  assert.deepEqual(fmt.changedFields(original, { ...original, "mqtt.password": "new" }), { "mqtt.password": "new" });
  assert.deepEqual(fmt.changedFields({ "mqtt.password": "" }, { "mqtt.password": "********" }), {});
});

test("pin fields: empty means not connected", () => {
  assert.equal(fmt.parsePin(""), -1);
  assert.equal(fmt.parsePin("  "), -1);
  assert.equal(fmt.parsePin("21"), 21);
  assert.equal(fmt.parsePin(" 5 "), 5);
  assert.ok(Number.isNaN(fmt.parsePin("2a")));
  assert.equal(fmt.pinText(-1), "");
  assert.equal(fmt.pinText(13), "13");
  assert.equal(fmt.pinText(undefined), "");
});

test("box addresses after setup", () => {
  assert.deepEqual(fmt.boxAddresses({ device: { hostname: "tapwave-a1b2c3" }, wifi: { ip: "192.168.1.50" } }), [
    "http://tapwave-a1b2c3.local/",
    "http://192.168.1.50/",
  ]);
  assert.deepEqual(fmt.boxAddresses({}), []);
});

test("broker problems are worded by the UI", () => {
  assert.equal(fmt.brokerProblem("auth"), "The broker turned down the user name or password");
  assert.equal(fmt.brokerProblem("tls"), "The encrypted connection to the broker failed");
  assert.equal(fmt.brokerProblem(undefined), "The broker cannot be reached");
  assert.equal(fmt.brokerProblem("something new"), "The broker cannot be reached");
});

test("update phases", () => {
  assert.equal(fmt.updatePhase({ phase: "downloading", target: "web interface" }), "Downloading the web interface");
  assert.equal(fmt.updatePhase({ phase: "writing" }), "Installing the firmware");
  assert.equal(fmt.updatePhase({ phase: "restarting" }), "Restarting");
  assert.equal(fmt.updatePhase({ phase: "idle" }), "");
  assert.equal(fmt.updatePhase(null), "");
});
