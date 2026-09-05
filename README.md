# network-adblocker — whole-home ad blocking on an ESP32-S3

Firmware name: `netmon` (the board answers at http://netmon.local).

One ESP32-S3 on your Wi-Fi that:
- **blocks ads and trackers for every device** (a DNS sinkhole, Pi-hole style): ~440,000 domains from three
  lists (StevenBlack, oisd big, hagezi Multi PRO) held in PSRAM as sorted hashes, cached in flash, refreshed
  weekly or whenever the list config changes; A/AAAA answers of 0.0.0.0 / ::, everything else forwarded to
  Cloudflare (Google as fallback)
- **watches your internet**: probes the router and the internet every 5 s, tracks latency, loss and
  outages, and shows status on the onboard RGB LED
- **serves a live dashboard** at http://netmon.local (or its fixed IP): blocker stats per device,
  recent queries, pause / resume / update buttons, latency sparklines, outage log

## Files
- `invidious/` — a private, ad-free YouTube front-end for the household (see below)
- `netmon/netmon.ino` — monitor, web server, dashboard
- `netmon/dnsblock.cpp` / `.h` — the DNS server, blocklist fetch/cache, stats
- `netmon/dnsconfig.h` — blocklist URLs, allow/extra-block lists, upstream resolvers
- `netmon/dnsconfig.h` also holds `LOCAL_NAMES`: names the board answers itself, e.g. `yt.home` → the front-end host
- `netmon/secrets.h` — Wi-Fi credentials, fixed IP, optional ntfy topic (gitignored; see `secrets.example.h`)
- `build.sh` — `./build.sh compile | upload | monitor | all` (set `PORT=` to override the serial port)

## Turning it on for the whole network
1. Flash it and check the dashboard says **BLOCKING** with a domain count.
2. In the router admin, set the **DNS server handed out by DHCP** to the board's fixed IP (192.168.1.53).
   Leave the secondary DNS empty, otherwise devices will sometimes bypass the blocker.
3. Reconnect devices to Wi-Fi (or wait for their DHCP lease to renew). They now use the ESP32 for DNS.

To turn it off: put the router's DNS setting back. Nothing else changes.

## Blocklists
Edit `netmon/dnsconfig.h`. Accepts hosts format, plain domains, `*.domain` wildcards and AdGuard `||domain^`.
Sizes as of Sep 2026: StevenBlack 80k · oisd big 256k · hagezi pro 225k → 440k unique (3.5 MB PSRAM).
Budget is ~480k unique domains; hagezi's threat-intel list (2.2M) is far too big for this board.
Note: oisd's server rejects HTTP/1.0, hence the streaming HTTP/1.1 downloader.

## YouTube ads: the Invidious front-end
DNS blocking cannot remove YouTube's own video ads (they stream from the same servers as the video), so
`invidious/` runs [Invidious](https://github.com/iv-org/invidious), an open-source YouTube front-end with no
ads or tracking, as three Docker containers (web app, "companion" that handles Google's anti-bot checks,
Postgres). Open **http://yt.home** on any device that uses the board for DNS (or `http://<host-ip>/`).

- Start / update: `cd invidious && ./setup.sh` (`--update` pulls new images, `--new-keys` regenerates secrets)
- Invidious must be restarted at least daily: `./setup.sh --daily-restart` installs a 04:30 launchd job on macOS
- Secrets live in `invidious/.env` (gitignored); `invidious/upstream/` is a sparse clone for the DB schema
- The host must stay on. Currently the MacBook Pro (192.168.1.152); plan is a Raspberry Pi 5. When it moves,
  change the IP in `LOCAL_NAMES` and reflash.
- Known: the JSON API (`/api/v1/videos/…`, used by third-party apps) crashes on storyboards with the current
  companion; the website and playback (360p progressive and DASH up to 1080p) work.
- Browser tip: the LibRedirect extension rewrites every youtube.com link to your instance automatically.

## API
`GET /api/status`, `GET /api/history`, `GET /api/dns`, `POST /api/pause?min=N`, `POST /api/resume`, `POST /api/update`

LED: green OK · amber degraded · red internet down · pink router unreachable · blue connecting
