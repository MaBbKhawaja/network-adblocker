// netmon YouTube guard — background worker (Chrome: service worker, Firefox: event page).
//
// Talks to the ESP32 so every browser in the house shares one rule set and one dashboard:
//   GET  http://netmon.local/api/yt/rules  -> { enabled, paused_s, rules: {...} }   every 5 minutes
//   POST http://netmon.local/api/yt/stats  <- { blocked, skipped, hidden }          every minute, if non-zero
// "enabled" follows the dashboard's Pause button, so pausing the DNS blocker pauses this too.
// If the ESP32 is unreachable the last rules from storage are used, then the bundled rules-default.json.
'use strict';
const api = globalThis.browser ?? globalThis.chrome;
const HOSTS = ['http://adblocker.local', 'http://192.168.1.53', 'http://vpn.local', 'http://192.168.1.54', 'http://netmon.local'];
const RULES_PATH = '/api/yt/rules';
const STATS_PATH = '/api/yt/stats';
let pending = { blocked: 0, skipped: 0, hidden: 0 };
let lastFetch = 0;   // last successful rules fetch; also how the board knows this browser has the extension

async function fetchWithTimeout(url, opts = {}, ms = 4000) {
  const c = new AbortController();
  const t = setTimeout(() => c.abort(), ms);
  try { return await fetch(url, { ...opts, signal: c.signal, cache: 'no-store' }); }
  finally { clearTimeout(t); }
}

async function bundledRules() {
  const r = await fetch(api.runtime.getURL('rules-default.json'));
  return { enabled: true, rules: await r.json(), source: 'bundled', fetchedAt: Date.now() };
}

async function stored() { return (await api.storage.local.get('ytguard')).ytguard; }

function broadcast(data) {
  const msg = { type: 'rules', enabled: data.enabled, rules: data.rules };
  Promise.resolve(api.tabs.query({ url: ['*://*.youtube.com/*', '*://*.youtube-nocookie.com/*'] }))
    .then(tabs => tabs.forEach(t => { try { Promise.resolve(api.tabs.sendMessage(t.id, msg)).catch(() => {}); } catch (_) {} }))
    .catch(() => {});
}

async function refreshRules() {
  for (const host of HOSTS) {
    try {
      const r = await fetchWithTimeout(host + RULES_PATH);
      if (!r.ok) continue;
      const j = await r.json();
      if (!j || !j.rules) continue;
      const data = { enabled: j.enabled !== false, rules: j.rules, source: host, fetchedAt: Date.now() };
      lastFetch = Date.now();
      // tell the other boards too: their "Require the extension" rule needs to know this browser is protected
      HOSTS.filter(h => h !== host).forEach(h => fetchWithTimeout(h + RULES_PATH, {}, 3000).catch(() => {}));
      await api.storage.local.set({ ytguard: data });
      broadcast(data);
      return data;
    } catch (_) { /* try the next address */ }
  }
  const cur = await stored();
  if (cur) return cur;
  const d = await bundledRules();
  await api.storage.local.set({ ytguard: d });
  return d;
}

async function getRules() { return (await stored()) || refreshRules(); }

async function flushStats() {
  if (!pending.blocked && !pending.skipped && !pending.hidden) return;
  const body = JSON.stringify(pending);
  for (const host of HOSTS) {
    try {
      const r = await fetchWithTimeout(host + STATS_PATH, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body });
      if (r.ok) { pending = { blocked: 0, skipped: 0, hidden: 0 }; return; }
    } catch (_) {}
  }
}

api.runtime.onMessage.addListener((m, _sender, sendResponse) => {
  if (!m) return;
  if (m.type === 'getRules') { getRules().then(sendResponse, () => sendResponse(null)); return true; }
  if (m.type === 'ping') {   // a YouTube page opened: make sure the board has heard from us recently
    if (Date.now() - lastFetch > 120000) refreshRules();
    sendResponse({ ok: true });
    return;
  }
  if (m.type === 'stats') {
    pending.blocked += m.blocked | 0; pending.skipped += m.skipped | 0; pending.hidden += m.hidden | 0;
    sendResponse({ ok: true });
  }
});

function schedule() {
  api.alarms.create('ytguard-rules', { periodInMinutes: 5 });
  api.alarms.create('ytguard-stats', { periodInMinutes: 1 });
}
api.runtime.onInstalled.addListener(() => { schedule(); refreshRules(); });
if (api.runtime.onStartup) api.runtime.onStartup.addListener(() => { schedule(); refreshRules(); });
api.alarms.onAlarm.addListener(a => {
  if (a.name === 'ytguard-rules') refreshRules();
  else if (a.name === 'ytguard-stats') flushStats();
});
