// The Tapwave web UI. It talks to the box only through the public REST API and WebSocket
// (ARCHITECTURE.md section 13).

import { createApi, upload, ApiError } from "./lib/api.js";
import * as fmt from "./lib/format.js";
import { t } from "./lib/i18n.js";

const api = createApi();

// ---------------------------------------------------------------------------- DOM helpers

function h(tag, props = {}, ...children) {
  const el = document.createElement(tag);
  for (const [key, value] of Object.entries(props)) {
    if (value === undefined || value === null || value === false) continue;
    if (key === "class") el.className = value;
    else if (key === "text") el.textContent = value;
    else if (key.startsWith("on")) el.addEventListener(key.slice(2).toLowerCase(), value);
    else if (key in el && key !== "list" && key !== "form") el[key] = value;
    else el.setAttribute(key, value === true ? "" : value);
  }
  for (const child of children.flat(Infinity)) {
    if (child === null || child === undefined || child === false) continue;
    el.append(child instanceof Node ? child : document.createTextNode(String(child)));
  }
  return el;
}

const ICONS = {
  home: '<path d="M4 11.2 12 4.5l8 6.7V19a1.5 1.5 0 0 1-1.5 1.5H15v-5.5H9v5.5H5.5A1.5 1.5 0 0 1 4 19z"/>',
  cards:
    '<path d="M8 4.5h10A2.5 2.5 0 0 1 20.5 7v8.5"/><rect x="3.5" y="8.5" width="13" height="11" rx="2"/>',
  music: '<path d="M9 17.5V6.2l10-2v11.3"/><circle cx="6.5" cy="17.5" r="2.5"/><circle cx="16.5" cy="15.5" r="2.5"/>',
  settings:
    '<path d="M4 7h9M18 7h2M4 17h3M12 17h8"/><circle cx="15.5" cy="7" r="2.3"/><circle cx="9.5" cy="17" r="2.3"/>',
  update: '<path d="M12 4v10.5m0 0 4.2-4.2M12 14.5l-4.2-4.2M5 19.5h14"/>',
  wifi:
    '<path d="M2.8 9.6a13.5 13.5 0 0 1 18.4 0M5.8 13a9 9 0 0 1 12.4 0M8.9 16.3a4.6 4.6 0 0 1 6.2 0"/><circle cx="12" cy="19.4" r="1.2" fill="currentColor" stroke="none"/>',
  reader:
    '<rect x="3.5" y="3.5" width="17" height="17" rx="4"/><path d="M11 9.2a4 4 0 0 1 0 5.6M14 7a7 7 0 0 1 0 10"/><circle cx="8" cy="12" r="1.3" fill="currentColor" stroke="none"/>',
  hub: '<path d="M3.8 10.8 12 4.3l8.2 6.5V19a1.5 1.5 0 0 1-1.5 1.5H5.3A1.5 1.5 0 0 1 3.8 19z"/><circle cx="12" cy="14.2" r="2.2"/>',
  check: '<path d="m5 12.6 4.5 4.4L19 7.5"/>',
  alert:
    '<path d="M12 4.2 21 19.5H3z"/><path d="M12 10v4"/><circle cx="12" cy="16.8" r="1" fill="currentColor" stroke="none"/>',
  info: '<circle cx="12" cy="12" r="8.5"/><path d="M12 11v5"/><circle cx="12" cy="8" r="1" fill="currentColor" stroke="none"/>',
  chevron: '<path d="m9.5 6 6 6-6 6"/>',
  copy: '<rect x="9" y="9" width="11" height="11" rx="2.5"/><path d="M5.5 15V6.5A2 2 0 0 1 7.5 4.5H15"/>',
  lock: '<rect x="5" y="10.5" width="14" height="10" rx="2.5"/><path d="M8 10.5V8a4 4 0 0 1 8 0v2.5"/>',
  eye: '<path d="M2.5 12S6 5.5 12 5.5 21.5 12 21.5 12 18 18.5 12 18.5 2.5 12 2.5 12z"/><circle cx="12" cy="12" r="3"/>',
  refresh: '<path d="M19.5 12a7.5 7.5 0 1 1-2.2-5.3M19.5 4.5V9H15"/>',
  power: '<path d="M12 3.5v8M7.2 6.4a7.5 7.5 0 1 0 9.6 0"/>',
  link: '<path d="M10.5 13.5a4 4 0 0 0 5.6 0l3-3a4 4 0 0 0-5.6-5.6l-1 1M13.5 10.5a4 4 0 0 0-5.6 0l-3 3a4 4 0 0 0 5.6 5.6l1-1"/>',
  text: '<path d="M5 6h14M5 10h14M5 14h9M5 18h6"/>',
  wave: '<path d="M9.5 8.5a5 5 0 0 1 0 7M13.5 5.5a9.5 9.5 0 0 1 0 13"/><circle cx="6" cy="12" r="1.6" fill="currentColor" stroke="none"/>',
  file: '<path d="M7 3.5h7l4.5 4.5v12a.5.5 0 0 1-.5.5H7a1.5 1.5 0 0 1-1.5-1.5v-14A1.5 1.5 0 0 1 7 3.5z"/><path d="M13.5 3.5V8.5h5"/>',
};

function icon(name) {
  const span = document.createElement("span");
  span.style.display = "contents";
  span.innerHTML = `<svg viewBox="0 0 24 24" width="24" height="24" fill="none" stroke="currentColor" stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${ICONS[name] || ""}</svg>`;
  return span.firstChild;
}

function readerGlyph() {
  const span = document.createElement("span");
  span.style.display = "contents";
  span.innerHTML =
    '<svg viewBox="0 0 84 84" fill="none" stroke="currentColor" stroke-linecap="round" aria-hidden="true">' +
    '<rect x="17" y="19" width="50" height="46" rx="12" stroke-width="3"/>' +
    '<g class="waves" stroke-width="3.5"><path d="M41 35a10 10 0 0 1 0 14"/><path d="M48 29a18 18 0 0 1 0 26"/></g>' +
    '<circle cx="33" cy="42" r="3.5" fill="currentColor" stroke="none"/></svg>';
  return span.firstChild;
}

let toastTimer;
function toast(message) {
  const el = document.getElementById("toast");
  el.textContent = message;
  el.dataset.show = "true";
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => (el.dataset.show = "false"), 2400);
}

// The box is served over plain HTTP, where navigator.clipboard is not available.
async function copy(text) {
  try {
    if (navigator.clipboard && window.isSecureContext) {
      await navigator.clipboard.writeText(text);
    } else {
      const area = h("textarea", { value: text, readonly: true, style: "position:fixed;opacity:0" });
      document.body.append(area);
      area.select();
      const ok = document.execCommand("copy");
      area.remove();
      if (!ok) throw new Error("blocked");
    }
    toast(t("common.copied"));
  } catch {
    toast(t("common.copy_failed"));
  }
}

function setProgress(bar, percent) {
  bar.firstChild.style.setProperty("--p", String(Math.max(0, Math.min(100, percent)) / 100));
}

function busy(button, on) {
  button.setAttribute("aria-busy", on ? "true" : "false");
  button.disabled = on;
}

function errorText(error) {
  if (error instanceof ApiError) {
    if (error.status === 0) return t("common.network_error");
    if (error.message) return error.message;
  }
  return t("common.unknown_error");
}

function problemLine(message, tone = "bad") {
  return h("p", { class: "problem", "data-tone": tone }, icon(tone === "bad" ? "alert" : "info"), h("span", { text: message }));
}

function banner(tone, iconName, ...body) {
  return h("div", { class: "banner", "data-tone": tone, role: tone === "bad" ? "alert" : "status" }, icon(iconName), h("div", { class: "banner-body" }, ...body));
}

// ---------------------------------------------------------------------------- state

const state = {
  status: null,
  scans: [],
  present: false,
  live: "connecting",
  update: null,
  setupOverride: false,
};
const listeners = new Set();

function emit(change) {
  for (const listener of listeners) listener(change);
}

function lastScan() {
  return state.scans[0] || null;
}

function scanTime(scan) {
  if (scan.time) return fmt.relativeTime(scan.time);
  if (scan._seen) return fmt.relativeTime(new Date(scan._seen).toISOString());
  return t("time.since_start");
}

async function restart() {
  try {
    await api.post("/api/device/restart");
    toast(t("common.restarting"));
  } catch (error) {
    toast(errorText(error));
  }
}

// ---------------------------------------------------------------------------- live updates

let socket;
let retry = 0;

function setLive(value) {
  state.live = value;
  const el = document.getElementById("live");
  el.dataset.state = value;
  el.textContent = t(`live.${value}`);
}

function connectLive() {
  const scheme = location.protocol === "https:" ? "wss" : "ws";
  socket = new WebSocket(`${scheme}://${location.host}/ws`);
  socket.onopen = () => {
    retry = 0;
    setLive("live");
  };
  socket.onmessage = (event) => {
    let message;
    try {
      message = JSON.parse(event.data);
    } catch {
      return;
    }
    if (message.type === "status") {
      state.status = message.status;
      state.present = Boolean(message.status.tag_present);
      showName();
      emit("status");
    } else if (message.type === "scan") {
      message.scan._seen = Date.now();
      state.scans = [message.scan, ...state.scans].slice(0, 20);
      state.present = true;
      emit("scan");
    } else if (message.type === "removed") {
      state.present = false;
      emit("removed");
    } else if (message.type === "update") {
      state.update = message.update;
      emit("update");
    }
  };
  socket.onclose = () => {
    setLive(retry === 0 && state.live === "connecting" ? "connecting" : "offline");
    const delay = Math.min(10000, 1000 * 2 ** retry++);
    setTimeout(connectLive, delay);
  };
}

function showName() {
  const name = state.status?.device?.name || "Tapwave";
  document.getElementById("device-name").textContent = name;
  document.title = name === "Tapwave" ? "Tapwave" : `${name} · Tapwave`;
}

// ---------------------------------------------------------------------------- navigation

const NAV = [
  ["", "nav.home", "home"],
  ["cards", "nav.cards", "cards"],
  ["library", "nav.library", "music"],
  ["settings", "nav.settings", "settings"],
  ["update", "nav.update", "update"],
];

function renderNav(current, hidden) {
  const nav = document.getElementById("nav");
  nav.hidden = hidden;
  nav.replaceChildren(
    ...NAV.map(([path, label, iconName]) =>
      h("a", { href: `#/${path}`, "aria-current": current === path ? "page" : undefined }, icon(iconName), h("span", { text: t(label) }))
    )
  );
}

let page = null;

function mount(view) {
  if (page?.destroy) page.destroy();
  page = view;
  const main = document.getElementById("main");
  main.replaceChildren(view.el);
}

function route() {
  const [, path = "", sub = ""] = location.hash.replace(/^#/, "").split("/");
  const setup = state.setupOverride || state.status?.wifi?.state === "setup";
  if (setup) {
    renderNav("", true);
    mount(setupPage());
    return;
  }
  const pages = { "": homePage, cards: cardsPage, library: libraryPage, settings: settingsPage, update: updatePage };
  const make = pages[path] || homePage;
  renderNav(pages[path] ? path : "", false);
  mount(make(sub));
  if (location.hash !== "" && !sub) document.getElementById("main").focus({ preventScroll: true });
}

listeners.add((change) => page?.update?.(change));

// ---------------------------------------------------------------------------- home

function homePage() {
  const banners = h("div", { class: "stack" });

  // The last card panel: fixed cells, so a new card changes text in place.
  const reader = h("div", { class: "reader" }, readerGlyph());
  const title = h("h2", { class: "tap-title" });
  const id = h("p", { class: "tap-id" });
  const meaning = h("p", { class: "tap-meaning" });
  const problem = h("div");
  const when = h("span", { class: "muted small tap-time" });
  const presence = h("span", { class: "chip tap-state" });
  const copyButton = h("button", { class: "btn small tap-copy", type: "button", onclick: () => lastScan() && copy(lastScan().id) }, icon("copy"), t("common.copy_id"));
  const body = h("div", { class: "tap-body" }, id, meaning, problem);
  const foot = h("div", { class: "tap-foot" }, presence, when, copyButton);
  const panel = h("section", { class: "card tap", "aria-label": t("home.title"), "aria-live": "polite" }, reader, title, body, foot);

  const health = h("ul", { class: "list" });
  const facts = h("dl", { class: "facts" });
  const about = h("details", { class: "card pad advanced", style: "border-top:0" }, h("summary", {}, icon("chevron"), t("home.device")), h("div", { style: "padding-top:12px" }, facts));

  function fillPanel(animate) {
    const scan = lastScan();
    const status = state.status;
    const noReader = status && !status.reader?.found;
    reader.dataset.present = String(Boolean(scan) && state.present);
    reader.dataset.waiting = String(!scan && !noReader);
    if (!scan) {
      title.textContent = noReader ? t("home.no_reader_title") : t("home.waiting_title");
      id.textContent = "";
      meaning.textContent = noReader ? t("home.no_reader_text") : t("home.waiting_text");
      problem.replaceChildren();
      foot.hidden = true;
      return;
    }
    const described = fmt.describeScan(scan);
    title.textContent = described.title;
    id.textContent = scan.id;
    meaning.textContent = described.meaning;
    problem.replaceChildren(described.problem ? problemLine(described.problem, "warn") : "");
    when.textContent = scanTime(scan);
    presence.textContent = state.present ? t("home.on_reader") : t("home.removed");
    presence.dataset.tone = state.present ? "ok" : "";
    foot.hidden = false;
    if (animate) {
      reader.classList.remove("ripple");
      body.classList.remove("settle");
      void reader.offsetWidth; // restart the animations
      reader.classList.add("ripple");
      body.classList.add("settle");
      title.classList.remove("settle");
      void title.offsetWidth;
      title.classList.add("settle");
    }
  }

  // State shows as the tint of the icon tile and in words; a row that needs something says what,
  // in orange, and the whole row leads there.
  function healthRow(href, iconName, label, detail, tone, stateText, action) {
    return h(
      "li",
      {},
      h(
        "a",
        { class: "item", href },
        h("span", { class: "item-icon", "data-tone": tone }, icon(iconName)),
        h(
          "span",
          { class: "item-text" },
          h("span", { class: "item-title", text: label }),
          h("span", { class: "item-detail", text: detail }),
          h("span", { class: "visually-hidden", text: stateText }),
          action ? h("span", { class: "item-action", text: action }) : ""
        ),
        h("span", { class: action ? "item-end is-action" : "item-end" }, icon("chevron"))
      )
    );
  }

  function fillHealth() {
    const s = state.status;
    if (!s) return;
    const rows = [];
    rows.push(
      s.reader.found
        ? healthRow("#/settings/reader", "reader", t("health.reader"), t("health.reader_ok", { model: s.reader.summary }), "ok", t("chip.ok"))
        : healthRow("#/settings/reader", "reader", t("health.reader"), t("health.reader_missing"), "bad", t("chip.problem"), t("health.reader_fix"))
    );
    const wifi = s.wifi;
    const sig = fmt.signal(wifi.rssi);
    const wifiRow = {
      connected: ["ok", t("chip.ok"), t("health.wifi_ok", { ssid: wifi.ssid, signal: sig.label })],
      connecting: ["warn", t("chip.wait"), t("health.wifi_connecting", { ssid: wifi.ssid })],
      offline: ["bad", t("chip.problem"), t("health.wifi_offline", { ssid: wifi.ssid }), t("health.wifi_fix")],
      setup: ["warn", t("chip.problem"), t("health.wifi_setup"), t("health.wifi_setup_fix")],
    }[wifi.state] || ["", "", ""];
    rows.push(healthRow("#/settings/wifi", "wifi", t("health.wifi"), wifiRow[2], wifiRow[0], wifiRow[1], wifiRow[3]));
    const m = s.mqtt;
    let ha;
    if (!m.enabled) ha = ["", t("chip.off"), t("health.ha_off"), t("health.ha_setup")];
    else if (m.connected) ha = ["ok", t("chip.ok"), t("health.ha_ok")];
    else if (m.error) ha = ["bad", t("chip.problem"), fmt.brokerProblem(m.error_code), t(m.error_code === "auth" ? "health.ha_fix_auth" : "health.ha_fix_other")];
    else ha = ["warn", t("chip.wait"), t("health.ha_waiting", { broker: m.broker })];
    rows.push(healthRow("#/settings/ha", "hub", t("health.ha"), ha[2], ha[0], ha[1], ha[3]));
    const u = s.update || {};
    let upd;
    if (["downloading", "writing", "restarting"].includes(u.phase)) upd = ["warn", t("chip.wait"), t("health.updates_running")];
    else if (u.available) upd = ["warn", t("chip.new"), t("health.updates_new", { latest: u.latest }), t("health.updates_fix")];
    else upd = ["ok", t("chip.ok"), t("health.updates_ok", { version: s.version })];
    rows.push(healthRow("#/update", "update", t("health.updates"), upd[2], upd[0], upd[1], upd[3]));
    health.replaceChildren(...rows);

    const factRows = [
      [t("facts.version"), s.version],
      [t("facts.address"), s.wifi.ip || s.wifi.access_point?.ip || "-"],
      [t("facts.hostname"), `${s.device.hostname}.local`],
      [t("facts.uptime"), fmt.duration(s.uptime)],
      [t("facts.chip"), s.device.chip],
      [t("facts.memory"), fmt.bytes(s.heap?.free)],
      [t("facts.mac"), s.device.mac],
    ];
    facts.replaceChildren(...factRows.flatMap(([k, v]) => [h("dt", { text: k }), h("dd", { text: v })]));
  }

  function fillBanners() {
    const s = state.status;
    const items = [];
    if (!s) return banners.replaceChildren();
    if (s.wifi.state === "offline" && s.wifi.access_point?.active) {
      items.push(
        banner("bad", "wifi", h("strong", { text: t("home.setup_lost", { ssid: s.wifi.ssid }) }), h("p", { text: t("home.setup_lost_text") }), h("div", {}, h("button", { class: "btn small", type: "button", onclick: startSetup }, t("home.change_wifi"))))
      );
    }
    if (s.restart_required) {
      items.push(banner("warn", "info", h("strong", { text: t("home.restart_needed") }), h("div", {}, h("button", { class: "btn small", type: "button", onclick: restart }, icon("power"), t("common.restart")))));
    }
    if (s.config_issues?.length) {
      items.push(banner("warn", "alert", h("strong", { text: t("home.config_issue") }), ...s.config_issues.map((i) => h("p", { class: "small", text: i.message }))));
    }
    banners.replaceChildren(...items);
  }

  const el = h(
    "div",
    { class: "page" },
    h("h1", { class: "visually-hidden", text: t("home.title") }),
    banners,
    h("div", { class: "home-grid" }, panel, h("div", { class: "stack" }, h("section", { class: "card", "aria-label": t("home.health") }, health), about))
  );

  fillPanel(false);
  fillHealth();
  fillBanners();
  const ticker = setInterval(() => lastScan() && (when.textContent = scanTime(lastScan())), 10000);

  return {
    el,
    update(change) {
      if (change === "scan") fillPanel(true);
      if (change === "removed") fillPanel(false);
      if (change === "status") {
        fillPanel(false);
        fillHealth();
        fillBanners();
      }
    },
    destroy() {
      clearInterval(ticker);
    },
  };
}

// ---------------------------------------------------------------------------- cards

const KIND_ICON = { ha_tag: "hub", tapwave: "wave", music: "music", url: "link", text: "text", uid: "cards" };

function cardsPage() {
  const list = h("ul", { class: "list" });
  const listCard = h("section", { class: "card" }, list);
  const open = new Set();

  function row(scan, index) {
    const described = fmt.describeScan(scan);
    const key = `${scan.uid}-${scan.time || scan._seen || index}`;
    const expanded = open.has(key);
    const details = h(
      "div",
      { hidden: !expanded, class: "stack", style: "padding:0 14px 16px" },
      h(
        "ul",
        { class: "records" },
        scan.records?.length
          ? scan.records.map((record) => {
              const r = fmt.describeRecord(record);
              return h("li", {}, h("b", { text: r.label }), r.value);
            })
          : h("li", { text: t("cards.no_records") }),
        h("li", {}, h("b", { text: t("cards.uid") }), scan.uid),
        h("li", {}, h("b", { text: t("cards.type") }), scan.tag || "-")
      ),
      described.problem ? problemLine(described.problem, "warn") : "",
      h("div", { style: "padding-left:58px" }, h("button", { class: "btn secondary small", type: "button", onclick: () => copy(scan.id) }, icon("copy"), t("common.copy_id")))
    );
    const button = h(
      "button",
      {
        class: "item",
        type: "button",
        "aria-expanded": String(expanded),
        onclick: () => {
          const now = !open.has(key);
          now ? open.add(key) : open.delete(key);
          button.setAttribute("aria-expanded", String(now));
          details.hidden = !now;
        },
      },
      h("span", { class: "item-icon" }, icon(KIND_ICON[scan.kind] || "cards")),
      h("span", { class: "item-text" }, h("span", { class: "item-title", text: described.title }), h("span", { class: "item-detail", text: `${scan.id} · ${scanTime(scan)}` })),
      h("span", { class: "item-end" }, scan.ndef_error ? h("span", { class: "chip", "data-tone": "warn", text: "!" }) : "", icon("chevron"))
    );
    return h("li", {}, button, details);
  }

  function fill() {
    if (!state.scans.length) {
      listCard.replaceChildren(
        h("div", { class: "pad empty" }, h("span", { class: "empty-icon" }, icon("cards")), h("h2", { text: t("cards.empty_title") }), h("p", { class: "muted", text: t("cards.empty_text") }))
      );
      return;
    }
    list.replaceChildren(...state.scans.map(row));
    listCard.replaceChildren(list);
  }

  const el = h(
    "div",
    { class: "page narrow" },
    h("div", { class: "page-head" }, h("h1", { text: t("cards.title") }), h("p", { text: t("cards.intro") })),
    listCard,
    banner("calm", "info", h("p", { text: t("cards.coming") }))
  );
  fill();
  return {
    el,
    update(change) {
      if (change === "scan") fill();
    },
  };
}

// ---------------------------------------------------------------------------- library

function libraryPage() {
  return {
    el: h(
      "div",
      { class: "page" },
      h("div", { class: "page-head" }, h("h1", { text: t("library.title") })),
      h(
        "section",
        { class: "card pad empty" },
        h("span", { class: "empty-icon" }, icon("music")),
        h("h2", { text: t("library.heading") }),
        h("p", { class: "muted", text: t("library.text") }),
        h("a", { class: "btn", href: "#/settings/ha" }, t("library.cta"))
      )
    ),
  };
}

// ---------------------------------------------------------------------------- settings

// Field definitions: key is "section.name" in the configuration.
function field(def, value, onInput) {
  const id = `f-${def.key.replace(".", "-")}`;
  const hintId = `${id}-hint`;
  const problemBox = h("div", { "data-problems": def.key });
  let control;
  if (def.type === "switch") {
    const input = h("input", { type: "checkbox", id, role: "switch", checked: Boolean(value), disabled: def.disabled, "aria-describedby": def.hint ? hintId : undefined, onchange: (e) => onInput(def.key, e.target.checked) });
    return h(
      "div",
      { class: "field", "data-key": def.key },
      h("div", { class: "switch-row" }, h("div", {}, h("label", { for: id, text: def.label }), def.hint ? h("p", { class: "hint", id: hintId, text: def.hint }) : ""), h("span", { class: "switch" }, input, h("span"))),
      problemBox
    );
  }
  if (def.type === "range") {
    control = h("input", { type: "range", id, min: def.min, max: def.max, value, onInput: (e) => onInput(def.key, Number(e.target.value)) });
  } else if (def.type === "select") {
    control = h("select", { class: "input", id, onchange: (e) => onInput(def.key, e.target.value) }, def.options.map(([v, label]) => h("option", { value: v, selected: v === value, text: label })));
  } else {
    const isPin = def.type === "pin";
    const isNumber = def.type === "number";
    control = h("input", {
      class: "input",
      id,
      type: def.type === "password" ? "password" : "text",
      inputMode: isPin || isNumber ? "numeric" : undefined,
      value: isPin ? fmt.pinText(value) : value ?? "",
      placeholder: def.placeholder,
      autocomplete: def.type === "password" ? "new-password" : "off",
      spellcheck: false,
      disabled: def.disabled,
      "aria-describedby": def.hint ? hintId : undefined,
      onInput: (e) => {
        const raw = e.target.value;
        onInput(def.key, isPin ? fmt.parsePin(raw) : isNumber ? (raw.trim() === "" ? Number.NaN : Number(raw)) : raw);
      },
    });
  }
  return h("div", { class: "field", "data-key": def.key }, h("label", { for: id, text: def.label }), control, def.hint ? h("p", { class: "hint", id: hintId, text: def.hint }) : "", problemBox);
}

function settingsForm(config, { id, title, intro, fields, advanced = [], before = [], after = [], startOpen = false, onSaved }) {
  const original = {};
  const values = {};
  for (const def of [...fields, ...advanced]) {
    const [section, key] = def.key.split(".");
    original[def.key] = config[section]?.[key];
    values[def.key] = original[def.key];
  }
  const save = h("button", { class: "btn", type: "submit" }, t("common.save"));
  const actions = h("div", { class: "form-actions", hidden: true }, save);
  const formProblem = h("div", { role: "alert" });

  function dirty() {
    return Object.keys(fmt.changedFields(original, values)).length > 0;
  }
  // Save appears once the section has a change, so untouched sections stay quiet.
  function onInput(key, value) {
    values[key] = value;
    actions.hidden = !dirty();
  }

  const body = [...fields.map((d) => field(d, values[d.key], onInput))];
  const advancedBlock = advanced.length
    ? h("details", { class: "advanced", open: startOpen }, h("summary", {}, icon("chevron"), t("common.advanced")), h("div", { class: "form" }, advanced.map((d) => field(d, values[d.key], onInput))))
    : "";

  const form = h(
    "form",
    {
      class: "form",
      novalidate: true,
      onsubmit: async (event) => {
        event.preventDefault();
        const changed = fmt.changedFields(original, values);
        const patch = {};
        for (const [path, value] of Object.entries(changed)) {
          const [section, key] = path.split(".");
          (patch[section] ||= {})[key] = Number.isNaN(value) ? "not a number" : value;
        }
        form.querySelectorAll("[data-problems]").forEach((box) => box.replaceChildren());
        form.querySelectorAll(".field").forEach((f) => f.removeAttribute("data-invalid"));
        formProblem.replaceChildren();
        busy(save, true);
        try {
          const result = await api.patch("/api/config", patch);
          Object.assign(original, changed);
          for (const warning of result.warnings || []) showProblem(warning);
          toast(result.restart_required ? t("settings.saved_restart") : t("common.saved"));
          onSaved?.(changed);
          refreshStatus();
        } catch (error) {
          if (error.status === 401) return signIn();
          if (error.details?.length) {
            error.details.forEach(showProblem);
            formProblem.replaceChildren(problemLine(t("settings.invalid")));
          } else {
            formProblem.replaceChildren(problemLine(errorText(error)));
          }
        } finally {
          busy(save, false);
          actions.hidden = !dirty();
        }
      },
    },
    before,
    body,
    advancedBlock,
    after,
    formProblem,
    actions
  );

  function showProblem(problem) {
    const box = form.querySelector(`[data-problems="${problem.path}"]`);
    if (!box) {
      formProblem.append(problemLine(problem.message, problem.warning ? "warn" : "bad"));
      return;
    }
    box.append(problemLine(problem.message, problem.warning ? "warn" : "bad"));
    if (!problem.warning) box.closest(".field").dataset.invalid = "true";
  }

  return h("section", { class: "card pad stack", id: `section-${id}` }, h("h2", { text: title }), intro ? h("p", { class: "muted", text: intro }) : "", form);
}

const PIN_GROUPS = [
  ["settings.pins.reader", ["i2c_sda", "i2c_scl", "spi_sck", "spi_miso", "spi_mosi", "nfc_cs", "nfc_reset", "nfc_irq"]],
  ["settings.pins.feedback", ["led", "buzzer"]],
  ["settings.pins.buttons", ["button_previous", "button_play", "button_next", "encoder_a", "encoder_b", "encoder_button"]],
  ["settings.pins.audio", ["i2s_bclk", "i2s_lrck", "i2s_dout", "sd_cs"]],
  ["settings.pins.power", ["battery_adc"]],
];

function settingsPage(sub) {
  const content = h("div", { class: "stack" }, h("p", { class: "muted", text: t("common.loading") }));
  const restartBox = h("div");
  const el = h("div", { class: "page narrow" }, h("div", { class: "page-head" }, h("h1", { text: t("settings.title") }), h("p", { text: t("settings.intro") })), restartBox, content);
  let haStatus;
  let readerBox;

  function showRestart() {
    const needed = state.status?.restart_required;
    restartBox.replaceChildren(needed ? banner("warn", "info", h("strong", { text: t("home.restart_needed") }), h("div", {}, h("button", { class: "btn small", type: "button", onclick: restart }, icon("power"), t("common.restart")))) : "");
  }

  function fillHaStatus() {
    if (!haStatus) return;
    const m = state.status?.mqtt;
    if (!m || !m.enabled) return haStatus.replaceChildren();
    const text = m.connected ? t("settings.ha.status_ok", { broker: m.broker }) : m.error ? t("settings.ha.status_error", { error: fmt.brokerProblem(m.error_code) }) : t("settings.ha.status_waiting", { broker: m.broker });
    haStatus.replaceChildren(h("span", { class: "chip", "data-tone": m.connected ? "ok" : m.error ? "bad" : "warn", text }));
  }

  async function fillReader() {
    if (!readerBox) return;
    try {
      const reader = await api.get("/api/reader");
      readerBox.replaceChildren(
        h("p", { class: "row" }, h("span", { class: "chip", "data-tone": reader.found ? "ok" : "bad", text: reader.found ? t("settings.reader.found", { summary: reader.summary }) : t("settings.reader.missing") })),
        reader.attempts?.length ? h("details", { class: "advanced" }, h("summary", {}, icon("chevron"), t("settings.reader.attempts")), h("ul", { class: "records", style: "padding-left:0" }, reader.attempts.map((a) => h("li", {}, h("b", { text: a.driver }), a.result)))) : ""
      );
      return reader;
    } catch (error) {
      readerBox.replaceChildren(problemLine(errorText(error)));
      return null;
    }
  }

  async function load() {
    let data;
    let reader = { drivers: [] };
    try {
      data = await api.get("/api/config");
      reader = (await api.get("/api/reader")) || reader;
    } catch (error) {
      if (error.status === 401) return signIn();
      content.replaceChildren(banner("bad", "alert", h("p", { text: errorText(error) }), h("div", {}, h("button", { class: "btn small", type: "button", onclick: load }, t("common.retry")))));
      return;
    }
    const config = data.config;
    const s = state.status;
    const sections = [];

    sections.push(
      settingsForm(config, {
        id: "box",
        title: t("settings.box"),
        fields: [{ key: "device.name", label: t("settings.box.name"), hint: t("settings.box.name_hint") }],
        advanced: [{ key: "device.hostname", label: t("settings.box.hostname"), hint: t("settings.box.hostname_hint", { default: data.defaults.hostname }), placeholder: data.defaults.hostname }],
        onSaved: () => refreshStatus(),
      })
    );

    sections.push(
      settingsForm(config, {
        id: "modes",
        title: t("settings.modes"),
        fields: [
          { key: "modes.home_assistant", type: "switch", label: t("settings.modes.ha"), hint: t("settings.modes.ha_hint") },
          { key: "modes.player", type: "switch", label: t("settings.modes.player"), hint: t("settings.modes.player_hint"), disabled: true },
        ],
      })
    );

    const wifiNow = s?.wifi?.state === "connected" ? t("settings.wifi.current", { ssid: s.wifi.ssid }) : t("settings.wifi.none");
    sections.push(
      settingsForm(config, {
        id: "wifi",
        title: t("settings.wifi"),
        before: [h("div", { class: "row" }, h("span", { class: "item-icon" }, icon("wifi")), h("strong", { text: wifiNow }), h("button", { class: "btn secondary small", type: "button", onclick: startSetup }, t("settings.wifi.change")))],
        fields: [],
        advanced: [{ key: "wifi.ap_password", type: "password", label: t("settings.wifi.ap_password"), hint: t("settings.wifi.ap_password_hint", { ap: s?.wifi?.access_point?.ssid || "Tapwave" }) }],
      })
    );

    haStatus = h("div");
    sections.push(
      settingsForm(config, {
        id: "ha",
        title: t("settings.ha"),
        intro: t("settings.ha.intro"),
        before: [haStatus],
        fields: [
          { key: "mqtt.enabled", type: "switch", label: t("settings.ha.enabled") },
          { key: "mqtt.host", label: t("settings.ha.host"), hint: t("settings.ha.host_hint"), placeholder: "homeassistant.local" },
          { key: "mqtt.username", label: t("settings.ha.username") },
          { key: "mqtt.password", type: "password", label: t("settings.ha.password") },
          { key: "mqtt.discovery", type: "switch", label: t("settings.ha.discovery"), hint: t("settings.ha.discovery_hint") },
        ],
        advanced: [
          { key: "mqtt.port", type: "number", label: t("settings.ha.port") },
          { key: "mqtt.tls", type: "switch", label: t("settings.ha.tls"), hint: t("settings.ha.tls_hint") },
          { key: "mqtt.topic_prefix", label: t("settings.ha.topic_prefix"), hint: t("settings.ha.topic_prefix_hint") },
          { key: "mqtt.discovery_prefix", label: t("settings.ha.discovery_prefix") },
        ],
      })
    );

    sections.push(
      settingsForm(config, {
        id: "feedback",
        title: t("settings.feedback"),
        fields: [
          { key: "feedback.led", type: "switch", label: t("settings.feedback.led"), hint: t("settings.feedback.led_hint") },
          { key: "feedback.led_brightness", type: "range", min: 0, max: 255, label: t("settings.feedback.brightness") },
          { key: "feedback.buzzer", type: "switch", label: t("settings.feedback.buzzer"), hint: t("settings.feedback.buzzer_hint") },
        ],
        advanced: [{ key: "feedback.led_count", type: "number", label: t("settings.feedback.led_count") }],
        after: [
          h("div", {}, h("button", {
            class: "btn quiet small",
            type: "button",
            onclick: async () => {
              try {
                await api.post("/api/device/identify");
              } catch (error) {
                toast(errorText(error));
              }
            },
          }, t("settings.feedback.test"))),
        ],
      })
    );

    readerBox = h("div", { class: "stack" });
    const detect = h("button", {
      class: "btn secondary small",
      type: "button",
      onclick: async () => {
        busy(detect, true);
        try {
          await api.post("/api/reader/detect");
          await new Promise((r) => setTimeout(r, 3000));
          await fillReader();
          refreshStatus();
        } catch (error) {
          toast(errorText(error));
        } finally {
          busy(detect, false);
        }
      },
    }, icon("refresh"), t("settings.reader.detect"));
    sections.push(
      settingsForm(config, {
        id: "reader",
        title: t("settings.reader"),
        before: [readerBox, h("div", {}, detect)],
        fields: [],
        advanced: [
          { key: "nfc.driver", type: "select", label: t("settings.reader.driver"), options: [["auto", t("settings.reader.driver_auto")], ...(reader.drivers || []).map((d) => [d.id, d.description])] },
          { key: "nfc.poll_interval_ms", type: "number", label: t("settings.reader.poll") },
          { key: "nfc.removal_polls", type: "number", label: t("settings.reader.removal") },
        ],
      })
    );

    const pinFields = PIN_GROUPS.flatMap(([, keys]) => keys.map((k) => ({ key: `pins.${k}`, type: "pin", label: t(`pin.${k}`), placeholder: "-" })));
    const pinsSection = settingsForm(config, { id: "pins", title: t("settings.pins"), intro: t("settings.pins.intro"), fields: [], advanced: pinFields, startOpen: sub === "pins" });
    // Group the pin fields under headings inside the disclosure.
    const pinForm = pinsSection.querySelector("details .form");
    if (pinForm) {
      const byKey = Object.fromEntries([...pinForm.children].map((f) => [f.dataset.key, f]));
      pinForm.replaceChildren(...PIN_GROUPS.map(([label, keys]) => h("fieldset", {}, h("legend", { text: t(label) }), h("div", { class: "fields-2" }, keys.map((k) => byKey[`pins.${k}`])))));
    }
    sections.push(pinsSection);

    const passwordSet = config.security.admin_password !== "";
    sections.push(
      settingsForm(config, {
        id: "password",
        title: t("settings.password"),
        intro: t("settings.password.intro"),
        before: [h("p", { class: "row" }, icon("lock"), h("span", { text: passwordSet ? t("settings.password.set") : t("settings.password.none") }))],
        fields: [{ key: "security.admin_password", type: "password", label: t("settings.password.new") }],
        after: passwordSet
          ? [h("div", {}, h("button", {
              class: "link-btn",
              type: "button",
              onclick: async () => {
                try {
                  await api.patch("/api/config", { security: { admin_password: "" } });
                  toast(t("common.saved"));
                  load();
                } catch (error) {
                  toast(errorText(error));
                }
              },
            }, t("settings.password.remove")))]
          : [],
        onSaved: async (changed) => {
          const password = changed["security.admin_password"];
          if (password) {
            try {
              await api.post("/api/session", { password });
            } catch {
              /* the next request asks for the password */
            }
          }
          load();
        },
      })
    );

    sections.push(
      settingsForm(config, {
        id: "system",
        title: t("settings.system"),
        fields: [{ key: "updates.check", type: "switch", label: t("settings.system.check") }],
        advanced: [
          { key: "updates.manifest_url", label: t("settings.system.manifest") },
          { key: "time.ntp_server", label: t("settings.system.ntp") },
        ],
      })
    );

    const confirmRow = h("div", { class: "form-actions", hidden: true });
    const eraseButton = h("button", { class: "btn secondary", type: "button", onclick: () => ((confirmRow.hidden = false), (eraseButton.hidden = true)) }, t("settings.erase"));
    confirmRow.append(
      h("button", {
        class: "btn danger",
        type: "button",
        onclick: async (event) => {
          busy(event.currentTarget, true);
          try {
            await api.post("/api/config/reset", { confirm: true });
            toast(t("common.restarting"));
          } catch (error) {
            toast(errorText(error));
          }
        },
      }, t("settings.erase.confirm")),
      h("button", { class: "btn quiet", type: "button", onclick: () => ((confirmRow.hidden = true), (eraseButton.hidden = false)) }, t("common.cancel")),
      h("span", { class: "muted small", text: t("settings.erase.sure") })
    );
    sections.push(h("section", { class: "card pad stack", id: "section-erase" }, h("h2", { text: t("settings.erase") }), h("p", { class: "muted", text: t("settings.erase.text") }), h("div", {}, eraseButton), confirmRow));

    content.replaceChildren(...sections);
    fillHaStatus();
    fillReader();
    if (sub) document.getElementById(`section-${sub}`)?.scrollIntoView({ block: "start" });
  }

  showRestart();
  load();
  return {
    el,
    update(change) {
      if (change === "status") {
        showRestart();
        fillHaStatus();
      }
    },
  };
}

// ---------------------------------------------------------------------------- update

function updatePage() {
  const head = h("h2");
  const line = h("p", { class: "muted" });
  const progress = h("div", { class: "progress", hidden: true }, h("span"));
  const phase = h("p", { class: "small", hidden: true });
  const problem = h("div");
  const check = h("button", { class: "btn", type: "button", onclick: () => start(false) }, icon("refresh"), t("update.check"));
  const install = h("button", { class: "btn", type: "button", hidden: true, onclick: () => start(true) });
  const notes = h("a", { hidden: true, target: "_blank", rel: "noopener", text: t("update.whats_new") });

  async function start(installing) {
    problem.replaceChildren();
    const button = installing ? install : check;
    busy(button, true);
    try {
      await api.post(installing ? "/api/update/install" : "/api/update/check");
    } catch (error) {
      if (error.status === 401) return signIn();
      problem.replaceChildren(problemLine(errorText(error)));
      busy(button, false);
    }
  }

  function fill() {
    const u = state.update;
    if (!u) return;
    head.textContent = t("update.installed", { version: u.installed });
    const running = ["checking", "downloading", "writing", "restarting"].includes(u.phase);
    busy(check, u.phase === "checking");
    busy(install, running && u.phase !== "checking");
    // One orange action at a time: Install when there is something to install, else Check.
    check.className = u.available ? "btn secondary" : "btn";
    if (u.available) {
      line.textContent = t("update.available", { latest: u.latest });
      install.hidden = false;
      install.replaceChildren(icon("update"), t("update.install", { latest: u.latest }));
    } else {
      line.textContent = u.latest ? t("update.up_to_date") : t("update.not_checked");
      install.hidden = true;
    }
    notes.hidden = !u.release_url;
    if (u.release_url) notes.href = u.release_url;
    const showProgress = u.phase === "downloading" || u.phase === "writing";
    progress.hidden = !showProgress;
    setProgress(progress, u.percent || 0);
    phase.hidden = !running;
    phase.textContent = `${fmt.updatePhase(u)}${showProgress ? ` ${u.percent || 0}%` : ""}`;
    problem.replaceChildren(u.phase === "failed" && u.error ? problemLine(t("update.failed", { error: u.error })) : "");
    if (u.phase === "restarting") waitForRestart();
  }

  function fileForm(label, target) {
    // The button stays off until there is a file to send.
    const send = h("button", { class: "btn secondary", type: "submit", disabled: true }, icon("file"), t("update.upload"));
    const input = h("input", { type: "file", accept: ".bin", id: `file-${target}`, onchange: () => (send.disabled = !input.files.length) });
    const bar = h("div", { class: "progress", hidden: true }, h("span"));
    const note = h("div");
    return h(
      "form",
      {
        class: "stack",
        onsubmit: async (event) => {
          event.preventDefault();
          note.replaceChildren();
          const file = input.files[0];
          if (!file) return note.replaceChildren(problemLine(t("update.pick_file")));
          busy(send, true);
          bar.hidden = false;
          try {
            await upload(`/api/update/${target}`, file, (percent) => {
              setProgress(bar, percent);
              send.lastChild.textContent = t("update.uploading", { percent });
            });
            note.replaceChildren(h("p", { class: "chip", "data-tone": "ok", text: t("common.restarting") }));
            waitForRestart();
          } catch (error) {
            if (error.status === 401) return signIn();
            note.replaceChildren(problemLine(errorText(error)));
            busy(send, false);
            send.lastChild.textContent = t("update.upload");
          }
        },
      },
      h("div", { class: "field" }, h("label", { for: `file-${target}`, text: label }), input),
      bar,
      note,
      h("div", {}, send)
    );
  }

  const el = h(
    "div",
    { class: "page narrow" },
    h("div", { class: "page-head" }, h("h1", { text: t("update.title") })),
    h("section", { class: "card pad stack" }, head, line, h("div", { class: "form-actions" }, install, check, notes), progress, phase, problem, h("p", { class: "muted small", text: t("update.keeps_working") })),
    h("section", { class: "card pad stack" }, h("h2", { text: t("update.files") }), h("p", { class: "muted", text: t("update.files_intro") }), h("div", { class: "fields-2" }, fileForm(t("update.firmware_file"), "firmware"), fileForm(t("update.fs_file"), "filesystem")))
  );

  api
    .get("/api/update")
    .then((u) => {
      state.update = u;
      fill();
    })
    .catch((error) => (error.status === 401 ? signIn() : problem.replaceChildren(problemLine(errorText(error)))));

  return {
    el,
    update(change) {
      if (change === "update") fill();
    },
  };
}

let waiting = false;
function waitForRestart() {
  if (waiting) return;
  waiting = true;
  toast(t("common.restarting"));
  const started = Date.now();
  const poll = async () => {
    try {
      const status = await api.get("/api/status");
      if (status.uptime < (Date.now() - started) / 1000 + 5) return location.reload();
    } catch {
      /* still restarting */
    }
    setTimeout(poll, 2000);
  };
  setTimeout(poll, 4000);
}

// ---------------------------------------------------------------------------- Wi-Fi setup

function startSetup() {
  state.setupOverride = true;
  route();
}

function setupPage() {
  const card = h("section", { class: "card pad stack solo" });
  const el = h("div", { class: "page" }, card);
  let ssid = "";
  let secure = true;
  let poller;

  function frame(step, heading, text, ...content) {
    const stepper = h("div", { class: "stepper", role: "img", "aria-label": t("setup.step", { n: step }) }, [1, 2, 3].map((i) => h("span", { "data-on": String(i <= step) })));
    card.replaceChildren(stepper, h("h1", { text: heading }), text ? h("p", { class: "muted", text }) : "", ...content);
    card.querySelector("h1").setAttribute("tabindex", "-1");
    card.querySelector("h1").focus({ preventScroll: true });
  }

  async function pick() {
    const list = h("ul", { class: "list networks", style: "padding:0" });
    const note = h("p", { class: "muted", text: t("setup.scanning") });
    const other = h("form", {
      class: "with-button",
      hidden: true,
      onsubmit: (e) => {
        e.preventDefault();
        const name = other.querySelector("input").value.trim();
        if (name) {
          ssid = name;
          secure = true;
          password();
        }
      },
    }, h("input", { class: "input", "aria-label": t("setup.other_name"), placeholder: t("setup.other_name") }), h("button", { class: "btn", type: "submit" }, t("setup.connect")));
    const refresh = h("button", { class: "btn secondary small", type: "button", onclick: () => load(true) }, icon("refresh"), t("setup.refresh"));
    const actions = h("div", { class: "form-actions" }, refresh, h("button", { class: "link-btn", type: "button", onclick: () => (other.hidden = false, other.querySelector("input").focus()) }, t("setup.other")));
    frame(1, t("setup.pick_title"), t("setup.pick_text"), note, list, actions, other);

    async function load(again) {
      clearTimeout(poller);
      busy(refresh, true);
      try {
        const data = await api.get("/api/wifi/networks");
        list.replaceChildren(
          ...data.networks.map((n) => {
            const sig = fmt.signal(n.rssi);
            return h("li", {}, h("button", {
              class: "item",
              type: "button",
              onclick: () => {
                ssid = n.ssid;
                secure = n.secure;
                secure ? password() : connect("");
              },
            }, h("span", { class: "item-icon" }, icon("wifi")), h("span", { class: "item-text" }, h("span", { class: "item-title", text: n.ssid }), h("span", { class: "item-detail", text: `${n.secure ? t("setup.secure") : t("setup.open")}, ${sig.label}` })), h("span", { class: "item-end" }, n.secure ? icon("lock") : "", icon("chevron"))));
          })
        );
        note.textContent = data.scanning ? t("setup.scanning") : data.networks.length ? "" : t("setup.none_found");
        note.hidden = !note.textContent;
        if (data.scanning || again) poller = setTimeout(() => load(false), 1500);
      } catch (error) {
        note.hidden = false;
        note.textContent = errorText(error);
      } finally {
        busy(refresh, false);
      }
    }
    load(false);
  }

  function password(problem) {
    const input = h("input", { class: "input", id: "wifi-password", type: "password", autocomplete: "current-password" });
    const show = h("input", { type: "checkbox", id: "show-password", onchange: (e) => (input.type = e.target.checked ? "text" : "password") });
    const form = h("form", {
      class: "form",
      novalidate: true,
      onsubmit: (e) => {
        e.preventDefault();
        const value = input.value;
        if (value.length < 8 || value.length > 64) {
          errorBox.replaceChildren(problemLine(t("setup.bad_password")));
          return;
        }
        connect(value);
      },
    });
    const errorBox = h("div", { role: "alert" }, problem ? problemLine(problem) : "");
    form.append(
      h("div", { class: "field" }, h("label", { for: "wifi-password", text: t("setup.password") }), input, h("label", { class: "row small", for: "show-password" }, show, t("setup.show_password"))),
      errorBox,
      h("div", { class: "form-actions" }, h("button", { class: "btn", type: "submit" }, t("setup.connect")), h("button", { class: "btn quiet", type: "button", onclick: pick }, t("setup.back")))
    );
    frame(2, t("setup.password_title", { ssid }), t("setup.password_text"), form);
    input.focus();
  }

  async function connect(pass) {
    frame(3, t("setup.connecting_title", { ssid }), t("setup.connecting_text"), h("div", { class: "progress" }, h("span", { style: "--p:0.35" })));
    try {
      await api.post("/api/wifi", { ssid, password: pass });
    } catch (error) {
      return secure ? password(errorText(error)) : pick();
    }
    const started = Date.now();
    const poll = async () => {
      if (Date.now() - started > 45000) return password(t("setup.failed", { ssid }));
      try {
        const status = await api.get("/api/status");
        state.status = status;
        if (status.wifi.attempt === "succeeded") return done(status);
        if (status.wifi.attempt === "failed") return secure ? password(t("setup.failed", { ssid })) : pick();
      } catch {
        /* the setup network can blink while the box joins */
      }
      poller = setTimeout(poll, 1000);
    };
    poller = setTimeout(poll, 1500);
  }

  function done(status) {
    const addresses = fmt.boxAddresses(status);
    card.replaceChildren(
      h("span", { class: "done-mark" }, icon("check")),
      h("h1", { text: t("setup.done_title", { ssid }) }),
      h("p", { class: "muted", text: t("setup.done_text") }),
      ...addresses.flatMap((address, i) => [i > 0 ? h("p", { class: "muted small", text: t("setup.done_or") }) : "", h("p", { class: "address", text: address })]),
      h("div", { class: "form-actions" }, h("button", { class: "btn", type: "button", onclick: () => copy(addresses[0] || "") }, icon("copy"), t("setup.copy_address")), h("button", {
        class: "btn quiet",
        type: "button",
        onclick: () => {
          state.setupOverride = false;
          location.hash = "#/";
          route();
        },
      }, t("setup.continue")))
    );
  }

  pick();
  return { el, destroy: () => clearTimeout(poller) };
}

// ---------------------------------------------------------------------------- sign-in

function signIn() {
  renderNav("", true);
  const input = h("input", { class: "input", id: "admin-password", type: "password", autocomplete: "current-password" });
  const errorBox = h("div", { role: "alert" });
  const submit = h("button", { class: "btn", type: "submit" }, t("signin.submit"));
  const form = h("form", {
    class: "form",
    onsubmit: async (e) => {
      e.preventDefault();
      busy(submit, true);
      try {
        await api.post("/api/session", { password: input.value });
        location.reload();
      } catch (error) {
        errorBox.replaceChildren(problemLine(error.status === 401 ? t("signin.wrong") : errorText(error)));
        busy(submit, false);
      }
    },
  }, h("div", { class: "field" }, h("label", { for: "admin-password", text: t("signin.password") }), input), errorBox, h("div", {}, submit));
  mount({ el: h("div", { class: "page" }, h("section", { class: "card pad stack solo" }, h("span", { class: "empty-icon" }, icon("lock")), h("h1", { text: t("signin.title") }), h("p", { class: "muted", text: t("signin.text") }), form)) });
  input.focus();
}

// ---------------------------------------------------------------------------- start

async function refreshStatus() {
  try {
    state.status = await api.get("/api/status");
    state.present = Boolean(state.status.tag_present);
    showName();
    emit("status");
  } catch {
    /* the WebSocket brings the next status */
  }
}

async function boot() {
  setLive("connecting");
  try {
    state.status = await api.get("/api/status");
    state.present = Boolean(state.status.tag_present);
    showName();
    if (state.status.last_scan) state.scans = [state.status.last_scan];
    if (state.status.wifi.state !== "setup") {
      try {
        state.scans = (await api.get("/api/scans")).scans;
      } catch (error) {
        if (error.status === 401) {
          connectLive();
          return signIn();
        }
      }
    }
  } catch {
    /* render with what we have; the WebSocket fills it in */
  }
  window.addEventListener("hashchange", route);
  route();
  connectLive();
}

boot();
