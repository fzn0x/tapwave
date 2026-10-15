// Pure helpers that turn API data into words. No DOM, so they run under `node --test`.

import { t } from "./i18n.js";

// "Just now", "3 minutes ago", ... from an ISO time. Without a time (the box had no clock yet)
// the caller shows something else.
export function relativeTime(iso, now = Date.now()) {
  const then = Date.parse(iso);
  if (Number.isNaN(then)) {
    return "";
  }
  const seconds = Math.max(0, Math.round((now - then) / 1000));
  if (seconds < 10) return t("time.just_now");
  if (seconds < 60) return t("time.seconds", { n: seconds });
  const minutes = Math.floor(seconds / 60);
  if (minutes === 1) return t("time.minute");
  if (minutes < 60) return t("time.minutes", { n: minutes });
  const hours = Math.floor(minutes / 60);
  if (hours === 1) return t("time.hour");
  if (hours < 24) return t("time.hours", { n: hours });
  const days = Math.floor(hours / 24);
  if (days === 1) return t("time.yesterday");
  return t("time.days", { n: days });
}

export function duration(seconds) {
  const s = Math.max(0, Math.floor(seconds));
  const d = Math.floor(s / 86400);
  const h = Math.floor((s % 86400) / 3600);
  const m = Math.floor((s % 3600) / 60);
  if (d > 0) return t("duration.days", { d, h });
  if (h > 0) return t("duration.hours", { h, m });
  return t("duration.minutes", { m });
}

export function bytes(n) {
  if (!Number.isFinite(n)) return "";
  if (n >= 1024 * 1024) return `${(n / (1024 * 1024)).toFixed(1)} MB`;
  if (n >= 1024) return `${Math.round(n / 1024)} KB`;
  return `${n} B`;
}

// Wi-Fi strength in words and bars (0 to 4).
export function signal(rssi) {
  if (!Number.isFinite(rssi) || rssi === 0) return { bars: 0, label: "" };
  if (rssi >= -55) return { bars: 4, label: t("signal.excellent") };
  if (rssi >= -67) return { bars: 3, label: t("signal.good") };
  if (rssi >= -75) return { bars: 2, label: t("signal.fair") };
  return { bars: 1, label: t("signal.weak") };
}

function actionText(action) {
  const target = action.target || "";
  const key = `action.${action.name}`;
  return t(key, { target });
}

// What a scan means, in a parent's words: a title and one sentence.
export function describeScan(scan) {
  const kind = scan.kind || "uid";
  const title = scan.text || t(`kind.${kind}`);
  let meaning;
  switch (kind) {
    case "ha_tag":
      meaning = t("meaning.ha_tag");
      break;
    case "tapwave": {
      const action = scan.action || {};
      if (action.error) {
        meaning = t("meaning.tapwave_broken", { reason: action.error });
      } else if (action.supported === false) {
        meaning = t("meaning.tapwave_unknown");
      } else if (action.name === "ha") {
        meaning = t("meaning.tapwave", { action: actionText(action) });
      } else {
        meaning = t("meaning.tapwave_later", { action: actionText(action) });
      }
      break;
    }
    case "music":
      meaning = t("meaning.music");
      break;
    case "url":
      meaning = t("meaning.url", { uri: scan.uri || "" });
      break;
    case "text":
      meaning = t("meaning.text", { text: scan.text || "" });
      break;
    default:
      meaning = t("meaning.uid");
  }
  const problem = scan.ndef_error ? t("meaning.ndef_problem", { reason: scan.ndef_error }) : "";
  return { title, kind: t(`kind.${kind}`), meaning, problem };
}

// One NDEF record as a label and a value.
export function describeRecord(record) {
  if (record.uri !== undefined) return { label: t("cards.record_uri"), value: record.uri };
  if (record.text !== undefined) {
    return { label: t("cards.record_text", { lang: record.lang || "?" }), value: record.text };
  }
  const value = record.payload_text ?? record.payload ?? "";
  return { label: t("cards.record_other", { type: record.type || "?" }), value };
}

// Field problems from a 422 answer, keyed by "section.key".
export function problemsByPath(details = []) {
  const out = {};
  for (const item of details) {
    if (!item || !item.path) continue;
    const list = out[item.path] || (out[item.path] = []);
    list.push({ message: item.message, warning: Boolean(item.warning) });
  }
  return out;
}

// The fields of `edited` that differ from `original`, for a PATCH body. Secrets left at the
// redaction placeholder are not sent, so the stored value is kept.
export function changedFields(original = {}, edited = {}) {
  const out = {};
  for (const [key, value] of Object.entries(edited)) {
    if (value === "********") continue;
    if (original[key] !== value) out[key] = value;
  }
  return out;
}

// Turns a pin field's text into a number for the API: empty means not connected (-1).
export function parsePin(text) {
  const trimmed = String(text ?? "").trim();
  if (trimmed === "") return -1;
  if (!/^-?\d+$/.test(trimmed)) return Number.NaN;
  return Number(trimmed);
}

export function pinText(value) {
  return value === -1 || value === undefined || value === null ? "" : String(value);
}

// Where to open the box after setup: its .local name and its IP address.
export function boxAddresses(status) {
  const out = [];
  const host = status?.device?.hostname;
  if (host) out.push(`http://${host}.local/`);
  const ip = status?.wifi?.ip;
  if (ip) out.push(`http://${ip}/`);
  return out;
}

// The broker problem in the UI's own words, from the firmware's error code.
export function brokerProblem(code) {
  const known = ["auth", "refused", "tls", "unreachable"];
  return t(`health.ha_err_${known.includes(code) ? code : "unreachable"}`);
}

// Text for an update in progress.
export function updatePhase(update) {
  if (!update) return "";
  const target = update.target || "firmware";
  switch (update.phase) {
    case "checking":
      return t("update.phase.checking");
    case "downloading":
      return t("update.phase.downloading", { target });
    case "writing":
      return t("update.phase.writing", { target });
    case "restarting":
      return t("update.phase.restarting");
    default:
      return "";
  }
}
