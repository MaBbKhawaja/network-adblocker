// netmon YouTube guard — isolated-world half (normal content script, document_start).
//
// Responsibilities:
//   1. Fetch the rule set from the background worker (which gets it from the ESP32) and hand it to
//      prune.js in the page world as a JSON string on a DOM event.
//   2. Player watchdog: if an ad still reaches the player (#movie_player gets class "ad-showing"), mute,
//      jump to the end of the ad and press any skip button. Covers server-stitched ads that pruning
//      cannot remove.
//   3. Cosmetic: hide promoted items with CSS (hide.css has the defaults; extra selectors from the rules
//      are injected here) and close the "ad blockers are not allowed" dialog if it appears.
//   4. Count what was removed and send it to the background worker every 30 s; it forwards the totals to
//      the ESP32 dashboard.
(() => {
  'use strict';
  const api = globalThis.browser ?? globalThis.chrome;
  const DEFAULTS = {
    version: 0,
    hide: [],
    skip_buttons: ['.ytp-skip-ad-button', '.ytp-ad-skip-button', '.ytp-ad-skip-button-modern', '.ytp-ad-skip-button-slot button'],
    ad_player_classes: ['ad-showing', 'ad-interrupting'],
    enforcement: ['ytd-enforcement-message-view-model'],
  };
  let rules = DEFAULTS;
  let enabled = true;
  const counts = { blocked: 0, skipped: 0, hidden: 0 };
  const html = document.documentElement;
  let styleEl = null;

  const pushRules = () => {
    document.dispatchEvent(new CustomEvent('ytguard-rules', { detail: JSON.stringify({ enabled, rules }) }));
    if (html) html.setAttribute('data-ytguard', enabled ? String(rules.version || 0) : 'off');
    applyHide();
  };

  // One CSS rule per selector: a selector the browser does not understand (e.g. :has) must not take
  // the others down with it.
  function applyHide() {
    const css = enabled && Array.isArray(rules.hide) ? rules.hide.map(s => `${s}{display:none!important}`).join('\n') : '';
    if (!styleEl) { styleEl = document.createElement('style'); styleEl.id = 'ytguard-hide'; (document.head || html).appendChild(styleEl); }
    styleEl.textContent = css;
  }

  function applyData(d) {
    if (!d) return;
    rules = { ...DEFAULTS, ...(d.rules || {}) };
    enabled = d.enabled !== false;
    pushRules();
  }

  pushRules();   // defaults first, so the page world is armed before YouTube's scripts run
  try { api.runtime.sendMessage({ type: 'getRules' }).then(applyData).catch(() => {}); } catch (_) {}
  try { api.runtime.sendMessage({ type: 'ping' }).catch(() => {}); } catch (_) {}
  try { api.runtime.onMessage.addListener(m => { if (m && m.type === 'rules') applyData(m); }); } catch (_) {}

  document.addEventListener('ytguard-count', e => { counts.blocked += Number(e.detail) || 0; });

  // --- player watchdog -----------------------------------------------------------------------------
  const video = () => document.querySelector('video.html5-main-video');
  const adShowing = () => {
    const p = document.getElementById('movie_player');
    return !!p && rules.ad_player_classes.some(c => p.classList.contains(c));
  };
  let wasAd = false, mutedByUs = false;
  setInterval(() => {
    if (!enabled) return;
    const ad = adShowing();
    if (ad) {
      const v = video();
      if (v) {
        if (!v.muted) { v.muted = true; mutedByUs = true; }
        if (Number.isFinite(v.duration) && v.duration > 0.5) v.currentTime = v.duration;
      }
      for (const sel of rules.skip_buttons) {
        const b = document.querySelector(sel);
        if (b) { b.click(); break; }
      }
    } else if (wasAd) {
      counts.skipped++;
      const v = video();
      if (mutedByUs && v) v.muted = false;
      mutedByUs = false;
    }
    wasAd = ad;

    // "Ad blockers violate YouTube's Terms of Service" dialog: close it and keep playing.
    for (const sel of rules.enforcement) {
      const el = document.querySelector(sel);
      if (!el) continue;
      const dialog = el.closest('tp-yt-paper-dialog, ytd-popup-container') || el;
      dialog.remove();
      document.querySelectorAll('tp-yt-iron-overlay-backdrop').forEach(b => b.remove());
      const v = video();
      if (v && v.paused) v.play().catch(() => {});
      counts.hidden++;
    }
  }, 300);

  // Promoted items that reached the DOM anyway (hidden by CSS): count them once each.
  setInterval(() => {
    if (!enabled || !Array.isArray(rules.hide) || !rules.hide.length) return;
    for (const sel of rules.hide) {
      let els;
      try { els = document.querySelectorAll(sel); } catch (_) { continue; }
      els.forEach(el => { if (!el.dataset.ytguardSeen) { el.dataset.ytguardSeen = '1'; counts.hidden++; } });
    }
  }, 2000);

  // --- report --------------------------------------------------------------------------------------
  setInterval(() => {
    if (!counts.blocked && !counts.skipped && !counts.hidden) return;
    const msg = { type: 'stats', ...counts };
    counts.blocked = counts.skipped = counts.hidden = 0;
    try { api.runtime.sendMessage(msg).catch(() => {}); } catch (_) {}
  }, 30000);
})();
