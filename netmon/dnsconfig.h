#pragma once
#include <Arduino.h>
// ---------------- Ad-blocker settings (edit, then ./build.sh all) ----------------

// Blocklists, fetched over HTTPS, merged, cached in flash, refreshed every REFRESH_DAYS.
// Accepts "hosts" format (0.0.0.0 domain), plain one-domain-per-line, or AdGuard "||domain^".
// StevenBlack's unified list is the same default Pi-hole ships with: ~100k domains, curated
// to avoid breaking sites. Add more lists as extra lines; keep the trailing nullptr.
static const char* BLOCKLIST_URLS[] = {
  "https://raw.githubusercontent.com/StevenBlack/hosts/master/hosts",                       // ~80k  ads/trackers, very safe
  "https://big.oisd.nl/domainswild2",                                                       // ~256k ads/pop-unders/trackers, curated for zero breakage
  "https://raw.githubusercontent.com/hagezi/dns-blocklists/main/wildcard/pro-onlydomains.txt", // ~225k hagezi "Multi PRO" (their recommended list)
  nullptr
};
static const int REFRESH_DAYS = 7;

// Never block these (the domain and all its subdomains). Keep the trailing nullptr.
static const char* ALLOWLIST[] = {
  // "example.com",
  nullptr
};

// Always block these (the domain and all its subdomains), on top of the lists.
static const char* EXTRA_BLOCK[] = {
  // "annoying-tracker.example",
  nullptr
};

// Where non-blocked queries go. The second is tried if the first doesn't answer in 1.5 s.
#define UPSTREAM_DNS_1  IPAddress(1, 1, 1, 1)   // Cloudflare
#define UPSTREAM_DNS_2  IPAddress(8, 8, 8, 8)   // Google
