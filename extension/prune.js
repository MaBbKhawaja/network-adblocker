// netmon YouTube guard — page-world half.
//
// This file runs inside YouTube's own JavaScript world (manifest: world "MAIN", document_start), i.e.
// before any of YouTube's scripts. Its job is to strip advertising out of the JSON YouTube hands to its
// player and feeds, so the player never learns an ad exists. Same idea as uBlock Origin's "json-prune"
// and "set" scriptlets:
//   - JSON.parse and Response.json are wrapped: every parsed object that looks like YouTube data is
//     walked, ad payload keys (playerAds, adPlacements, adSlots, ...) are deleted and list items that
//     carry an ad renderer (adSlotRenderer, promotedSparklesWebRenderer, ...) are removed.
//   - ytInitialPlayerResponse / ytInitialData, which YouTube assigns inline in the HTML, get an accessor
//     property so the same pruning runs the moment they are set.
// Rules arrive from guard.js (the isolated-world half) as a JSON string on a DOM event; the built-in
// defaults below cover the moment before that happens. Every hook is wrapped so a bug here can never
// break the page: on any error the original value is returned untouched.
(() => {
  'use strict';
  if (window.__ytguard) return;

  const G = {
    enabled: true,
    keys: new Set(['playerAds', 'adPlacements', 'adSlots', 'adBreakHeartbeatParams', 'adBreakParams']),
    renderers: new Set([
      'adSlotRenderer', 'promotedSparklesWebRenderer', 'promotedSparklesTextSearchRenderer', 'displayAdRenderer',
      'bannerPromoRenderer', 'statementBannerRenderer', 'promotedVideoRenderer', 'compactPromotedVideoRenderer',
      'inFeedAdLayoutRenderer', 'mastheadAdRenderer', 'actionCompanionAdRenderer', 'searchPyvRenderer',
      'adsEngagementPanelContentRenderer', 'enforcementMessageViewModel',
    ]),
    pruned: 0,   // since the last report to guard.js
    total: 0,    // since page load
  };
  Object.defineProperty(window, '__ytguard', { value: G, enumerable: false, configurable: false, writable: false });

  // Only objects that look like YouTube's own data are walked; everything else is left alone.
  const MARKERS = ['responseContext', 'playerResponse', 'playabilityStatus', 'streamingData', 'contents',
    'onResponseReceivedActions', 'onResponseReceivedCommands', 'onResponseReceivedEndpoints',
    'continuationContents', 'adPlacements', 'playerAds', 'adSlots'];
  const isObj = v => v !== null && typeof v === 'object';
  const isPlain = v => isObj(v) && !Array.isArray(v);
  const looksLikeYt = o => isPlain(o) && MARKERS.some(k => k in o);

  // Does a list item carry an ad renderer within a few levels? (e.g. richItemRenderer.content.adSlotRenderer,
  // or openPopupAction.popup.enforcementMessageViewModel). Whole items are removed so no empty tile is left.
  function carriesAd(o, depth) {
    if (!isPlain(o) || depth > 4) return false;
    for (const k of Object.keys(o)) {
      if (G.renderers.has(k)) return true;
      if (isPlain(o[k]) && carriesAd(o[k], depth + 1)) return true;
    }
    return false;
  }

  function prune(node, depth) {
    if (!isObj(node) || depth > 40) return;
    if (Array.isArray(node)) {
      for (let i = node.length - 1; i >= 0; i--) {
        const item = node[i];
        if (isPlain(item) && carriesAd(item, 0)) { node.splice(i, 1); G.pruned++; }
        else prune(item, depth + 1);
      }
      return;
    }
    for (const k of Object.keys(node)) {
      if (G.keys.has(k)) { delete node[k]; G.pruned++; }
      else prune(node[k], depth + 1);
    }
  }

  function clean(value) {
    try { if (G.enabled && looksLikeYt(value)) prune(value, 0); } catch (_) { /* never break the page */ }
    return value;
  }

  // --- hooks -------------------------------------------------------------------------------------
  const nativeParse = JSON.parse;
  JSON.parse = function parse(text, reviver) { return clean(nativeParse.call(this, text, reviver)); };
  try { JSON.parse.toString = () => nativeParse.toString(); } catch (_) {}

  const nativeJson = Response.prototype.json;
  Response.prototype.json = function json() { return nativeJson.apply(this, arguments).then(clean); };

  for (const name of ['ytInitialPlayerResponse', 'ytInitialData']) {
    let value = clean(window[name]);
    try {
      Object.defineProperty(window, name, {
        configurable: true, enumerable: true,
        get() { return value; },
        set(v) { value = clean(v); },
      });
    } catch (_) {}
  }

  // --- talk to guard.js (isolated world): only strings cross the boundary reliably -------------------
  const html = document.documentElement;
  if (html) html.setAttribute('data-ytguard-main', '1');

  document.addEventListener('ytguard-rules', e => {
    try {
      const r = nativeParse.call(JSON, String(e.detail));
      G.enabled = r.enabled !== false;
      if (r.rules) {
        if (Array.isArray(r.rules.prune_keys) && r.rules.prune_keys.length) G.keys = new Set(r.rules.prune_keys);
        if (Array.isArray(r.rules.prune_renderers) && r.rules.prune_renderers.length) G.renderers = new Set(r.rules.prune_renderers);
      }
    } catch (_) {}
  });

  setInterval(() => {
    if (!G.pruned) return;
    G.total += G.pruned;
    if (html) html.setAttribute('data-ytguard-pruned', String(G.total));
    document.dispatchEvent(new CustomEvent('ytguard-count', { detail: String(G.pruned) }));
    G.pruned = 0;
  }, 3000);
})();
