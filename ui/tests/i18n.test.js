import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { test } from "node:test";

import { languages, setLanguage, strings, t } from "../lib/i18n.js";

test("placeholders are filled and unknown ones are left visible", () => {
  assert.equal(t("setup.password_title", { ssid: "Home" }), "Password for Home");
  assert.equal(t("setup.password_title"), "Password for {ssid}");
  assert.equal(t("time.minutes", { n: 3 }), "3 minutes ago");
});

test("a missing key shows the key instead of nothing", () => {
  assert.equal(t("no.such.key"), "no.such.key");
});

test("unknown languages fall back to English", () => {
  setLanguage("xx");
  assert.equal(t("nav.home"), "Home");
  setLanguage("en");
});

test("every language has every English key with the same placeholders", () => {
  const placeholders = (s) => [...s.matchAll(/\{(\w+)\}/g)].map((m) => m[1]).sort().join(",");
  for (const code of languages) {
    for (const [key, english] of Object.entries(strings.en)) {
      const translated = strings[code][key];
      assert.ok(translated !== undefined, `${code} is missing ${key}`);
      assert.equal(placeholders(translated), placeholders(english), `${code} ${key} placeholders`);
    }
  }
});

test("no string uses an em-dash", () => {
  for (const [code, table] of Object.entries(strings)) {
    for (const [key, value] of Object.entries(table)) {
      assert.ok(!value.includes(String.fromCharCode(0x2014)), `${code} ${key}`);
    }
  }
});

test("every key the UI asks for exists", () => {
  const sources = ["../app.js", "../lib/format.js"].map((f) => readFileSync(new URL(f, import.meta.url), "utf8"));
  const used = new Set();
  for (const source of sources) {
    for (const match of source.matchAll(/\bt\(\s*"([a-z_.0-9]+)"/g)) used.add(match[1]);
  }
  assert.ok(used.size > 50, "the scan found the UI's keys");
  for (const key of used) {
    assert.ok(key in strings.en, `missing string ${key}`);
  }
});
