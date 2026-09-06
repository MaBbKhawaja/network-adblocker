# netmon YouTube guard — browser extension

Removes YouTube ads inside the browser, the one place a DNS blocker cannot reach (YouTube serves ads
from the same hostnames as the videos). The ESP32 running `netmon` is the control plane: it serves the
rule set to every browser in the house, collects the counts for its dashboard, and its **Pause** button
pauses this too.

## How it works (four layers, same ideas as uBlock Origin)

1. **Network rules** (`dnr-rules.json`, declarativeNetRequest). Blocks the ad bookkeeping endpoints
   (`/api/stats/ads`, `/pagead/`, `/ptracking`, doubleclick, googlesyndication…). Cheap, but on its own
   this does not stop video ads.
2. **JSON pruning in YouTube's own JS world** (`prune.js`, runs before any YouTube script). `JSON.parse`,
   `Response.json` and the inline `ytInitialPlayerResponse` / `ytInitialData` globals are wrapped. Every
   object that looks like YouTube data is walked: ad payload keys (`playerAds`, `adPlacements`, `adSlots`…)
   are deleted and list items carrying an ad renderer (`adSlotRenderer`, `promotedSparklesWebRenderer`, the
   anti-adblock `enforcementMessageViewModel`…) are removed. The player never learns an ad exists, so
   nothing to skip and no ad-blocker nag. This is the layer that actually stops pre-rolls and mid-rolls.
3. **Player watchdog** (`guard.js`). If an ad still starts (server-stitched ads, new JSON shapes), the
   player gets class `ad-showing`: the video is muted, jumped to its end and any skip button is pressed.
   Also closes the "ad blockers are not allowed" dialog and resumes playback.
4. **Cosmetic** (`hide.css` + selectors from the rules). Hides promoted tiles, banners, overlays.

`background.js` fetches `http://adblocker.local/api/yt/rules` every 5 minutes (falls back to
`192.168.1.53`, then to the last stored rules, then to the bundled `rules-default.json`) and posts the
counts to `http://adblocker.local/api/yt/stats` once a minute. When the dashboard pauses the DNS blocker the
rules come back with `enabled: false` and the extension stands down.

## Install

Easiest: on the computer, open **http://adblocker.local/extension**, download the zip from the board and follow the
three steps there. The board also lists, on its dashboard, any device that opened YouTube in a browser without
the extension. The manual way:

- **Chrome / Edge / Brave**: `chrome://extensions` → Developer mode → **Load unpacked** → pick this
  `extension/` folder. Updates to the code need "Reload" there; rule changes need nothing.
- **Firefox** (128+): `about:debugging#/runtime/this-firefox` → **Load Temporary Add-on** → `manifest.json`.
  Temporary add-ons vanish on restart; for a permanent install use Firefox Developer Edition or ESR and set
  `xpinstall.signatures.required = false` in `about:config`, then `about:addons` → Install Add-on From File
  (zip this folder as `.xpi`).
- Check it: open any YouTube page, then `http://adblocker.local` → "YouTube ads (browser extension)" shows
  the browser within a minute. In devtools, `document.documentElement.dataset.ytguard` is the rules version
  (`off` while paused).

## Requiring it

The dashboard has a **Require the extension** switch. When it is on, the board answers 0.0.0.0 for youtube.com to any
device that has not had the extension check in during the last 30 minutes, so YouTube in a browser simply fails to
load until the extension is installed. The extension checks in on browser start, every 5 minutes, and whenever a
YouTube page opens, so an installed extension is recognised within seconds. Phones and TVs must be exempted by IP in
`netmon/dnsconfig.h` (`YT_ENFORCE_EXEMPT`), because they can't run browser extensions.

## Changing the rules

Edit `netmon/ytguard.h`, bump `YT_RULES_VERSION`, `./build.sh all` (that also re-packs this folder into the zip the board serves). Every browser picks the new list up
within 5 minutes. Keep `rules-default.json` in step so a browser that cannot reach the ESP32 behaves the
same. When YouTube renames something, the usual fix is one more entry in `prune_keys` or `prune_renderers`;
uBlock Origin's filter list is the reference for current names.

## Limits

- Browsers only. The YouTube apps on phones and TVs are out of reach; use ReVanced / NewPipe / SmartTube
  there.
- iOS Safari cannot run this kind of extension and the YouTube app pins its certificate, so nothing on the
  network helps Apple devices; a third-party client such as Yattee is the only ad-free route there.
- YouTube changes its data shapes and anti-adblock measures regularly. Expect to update the rule list now
  and then; the dashboard counts dropping to zero while ads reappear is the signal.

## Testing without clicking around

Headless smoke test: branded Google Chrome 137+ ignores `--load-extension`, so use Chrome for Testing
(`npx @puppeteer/browsers install chrome@stable`), start it with `--headless=new --remote-debugging-port=9333
--load-extension=$PWD/extension`, open a watch page over the DevTools protocol and read
`document.documentElement.dataset` — `ytguardMain` = page-world hook installed, `ytguard` = rules version,
`ytguardPruned` = ad entries removed so far. Verified on 2026-09-06: 99 entries pruned on one watch page,
`adPlacements` gone from `ytInitialPlayerResponse`, counts arrived on the ESP32 within 70 s.
