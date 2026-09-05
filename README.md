# netmon — ESP32-S3 internet monitor + DNS ad blocker

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
- `netmon/netmon.ino` — monitor, web server, dashboard
- `netmon/dnsblock.cpp` / `.h` — the DNS server, blocklist fetch/cache, stats
- `netmon/dnsconfig.h` — blocklist URLs, allow/extra-block lists, upstream resolvers
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

## API
`GET /api/status`, `GET /api/history`, `GET /api/dns`, `POST /api/pause?min=N`, `POST /api/resume`, `POST /api/update`

LED: green OK · amber degraded · red internet down · pink router unreachable · blue connecting
