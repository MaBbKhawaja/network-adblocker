/*
  dnsblock — a small DNS sinkhole for ESP32-S3.

  Listens on UDP 53. For each query it looks the name (and each parent domain) up in a
  sorted array of 64-bit FNV-1a hashes held in PSRAM. Blocked names get an immediate
  0.0.0.0 / :: answer; everything else is forwarded to an upstream resolver and the
  reply relayed back. The list is fetched over HTTPS, cached in the FFat flash
  filesystem so it is available instantly after a reboot, and refreshed periodically.
*/
#include "dnsblock.h"
#include "dnsconfig.h"
#include <WiFi.h>
#include <AsyncUDP.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <FFat.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ---------------- tunables ----------------
static const uint16_t DNS_PORT       = 53;
static const uint16_t UPSTREAM_PORT  = 5533;    // our local port for upstream traffic
static const int      MAX_PENDING    = 48;      // forwarded queries in flight
static const size_t   PKT_MAX        = 320;     // bytes of a query kept for retry
static const uint32_t RETRY_MS       = 1500;    // no answer -> resend to second upstream
static const uint32_t DROP_MS        = 4000;    // give up on a forwarded query
static const int      MAX_CLIENTS    = 48;
static const int      MAX_RECENT     = 60;
static const uint8_t  BLOCK_TTL      = 30;      // seconds a blocked answer may be cached
static const size_t   LIST_CAP_PSRAM = 480000;  // max entries while parsing (3.8 MB)
static const size_t   LIST_CAP_INT   = 20000;   // fallback if PSRAM is missing
static const size_t   MIN_LIST       = 1000;    // fewer than this = treat fetch as failed
static const char*    CACHE_FILE     = "/blocklist.bin";
static const char*    TEMP_FILE      = "/blocklist.tmp";
static const uint32_t CACHE_MAGIC    = 0x4C424D4E;   // "NMBL"

// ---------------- state ----------------
static AsyncUDP srv, up;

static uint64_t*    list = nullptr;          // sorted hashes
static size_t       listCount = 0;
static time_t       listFetched = 0;
static portMUX_TYPE listMux = portMUX_INITIALIZER_UNLOCKED;

static uint64_t allowH[32], extraH[32];
static int      allowN = 0, extraN = 0;

struct Pending {
  bool used; uint8_t tries; uint16_t ourId, origId, clientPort, len;
  uint32_t clientIp, sentMs; uint8_t pkt[PKT_MAX];
};
static Pending      pend[MAX_PENDING];
static uint16_t     nextId = 1;
static portMUX_TYPE pendMux = portMUX_INITIALIZER_UNLOCKED;

struct DnsClient { uint32_t ip, q, b, lastMs; };
struct Recent { uint32_t t, ip; uint16_t qtype; uint8_t blocked; char name[56]; };
struct Stats {
  uint32_t total, blocked, forwarded;
  DnsClient clients[MAX_CLIENTS]; int nClients;
  Recent recent[MAX_RECENT]; int recentHead, recentCount;
};
static Stats        st;      // live (written from the UDP task)
static Stats        snap;    // copy for JSON building
static portMUX_TYPE statMux = portMUX_INITIALIZER_UNLOCKED;

static volatile uint32_t timeouts = 0, overflowDrops = 0;
static volatile uint32_t pausedUntilMs = 0;
static volatile bool     updateRequested = false, updating = false;
static char              lastUpdateMsg[120] = "not fetched yet";
static uint32_t          lastAttemptMs = 0;
static volatile bool     cacheStale = false;   // cache built from a different URL set / old format

// ---------------- small helpers ----------------
static uint64_t fnv1a(const char* s, size_t n) {
  uint64_t h = 1469598103934665603ULL;
  while (n--) { h ^= (uint8_t)*s++; h *= 1099511628211ULL; }
  return h;
}
// Fingerprint of the configured list URLs, stored in the cache so a config change triggers a refetch.
static uint64_t urlsHash() {
  uint64_t h = 1469598103934665603ULL;
  for (int i = 0; BLOCKLIST_URLS[i]; i++) {
    for (const char* u = BLOCKLIST_URLS[i]; *u; u++) { h ^= (uint8_t)*u; h *= 1099511628211ULL; }
    h ^= '\n'; h *= 1099511628211ULL;
  }
  return h;
}
static int cmp64(const void* a, const void* b) {
  uint64_t x = *(const uint64_t*)a, y = *(const uint64_t*)b;
  return x < y ? -1 : (x > y ? 1 : 0);
}
static bool inSorted(const uint64_t* a, size_t n, uint64_t h) {
  size_t lo = 0, hi = n;
  while (lo < hi) { size_t m = lo + (hi - lo) / 2; if (a[m] < h) lo = m + 1; else if (a[m] > h) hi = m; else return true; }
  return false;
}
static bool inSmall(const uint64_t* a, int n, uint64_t h) { for (int i = 0; i < n; i++) if (a[i] == h) return true; return false; }
static time_t epochNow() { time_t t = time(nullptr); return (t > 1600000000) ? t : 0; }
static String jsonEsc(const char* s) {
  String o; o.reserve(strlen(s) + 4);
  for (; *s; s++) { if (*s == '"' || *s == '\\') o += '\\'; if ((uint8_t)*s >= 32) o += *s; }
  return o;
}

// Is this name (or any parent domain) blocked?
static bool isBlocked(const char* name) {
  size_t total = strlen(name);
  if (!total) return false;
  const uint64_t* arr; size_t n;
  portENTER_CRITICAL(&listMux); arr = list; n = listCount; portEXIT_CRITICAL(&listMux);
  const char* p = name;
  for (;;) {
    uint64_t h = fnv1a(p, total - (size_t)(p - name));
    if (inSmall(allowH, allowN, h)) return false;
    if (inSmall(extraH, extraN, h)) return true;
    if (arr && inSorted(arr, n, h)) return true;
    const char* dot = strchr(p, '.');
    if (!dot) return false;
    p = dot + 1;
    if (!strchr(p, '.')) return false;      // reached the bare TLD: stop
  }
}

static void noteQuery(uint32_t ip, const char* name, uint16_t qtype, bool blocked) {
  uint32_t t = (uint32_t)epochNow(), ms = millis();
  portENTER_CRITICAL(&statMux);
  st.total++; if (blocked) st.blocked++; else st.forwarded++;
  DnsClient* c = nullptr;
  for (int i = 0; i < st.nClients; i++) if (st.clients[i].ip == ip) { c = &st.clients[i]; break; }
  if (!c) {
    if (st.nClients < MAX_CLIENTS) c = &st.clients[st.nClients++];
    else { c = &st.clients[0]; for (int i = 1; i < MAX_CLIENTS; i++) if (st.clients[i].lastMs < c->lastMs) c = &st.clients[i]; }
    c->ip = ip; c->q = 0; c->b = 0;
  }
  c->q++; if (blocked) c->b++; c->lastMs = ms;
  Recent& r = st.recent[st.recentHead];
  r.t = t; r.ip = ip; r.qtype = qtype; r.blocked = blocked;
  strncpy(r.name, name, sizeof(r.name) - 1); r.name[sizeof(r.name) - 1] = 0;
  st.recentHead = (st.recentHead + 1) % MAX_RECENT;
  if (st.recentCount < MAX_RECENT) st.recentCount++;
  portEXIT_CRITICAL(&statMux);
}

// ---------------- DNS packet handling ----------------
// Returns the offset just past the question section, or 0 if this isn't a plain query.
static size_t parseQuery(const uint8_t* p, size_t len, char* name, size_t cap, uint16_t& qtype) {
  if (len < 17) return 0;
  if (p[2] & 0x80) return 0;                       // a response, not a query
  if (p[2] & 0x78) return 0;                       // opcode != standard query
  if (((p[4] << 8) | p[5]) != 1) return 0;         // exactly one question
  size_t i = 12, o = 0;
  while (i < len) {
    uint8_t l = p[i++];
    if (l == 0) break;
    if (l & 0xC0) return 0;                        // compression pointer: not expected here
    if (i + l > len || o + l + 2 >= cap) return 0;
    if (o) name[o++] = '.';
    for (uint8_t k = 0; k < l; k++) { char c = (char)p[i + k]; name[o++] = (c >= 'A' && c <= 'Z') ? c + 32 : c; }
    i += l;
  }
  name[o] = 0;
  if (i + 4 > len) return 0;
  qtype = (p[i] << 8) | p[i + 1];
  return i + 4;
}

// Build a "blocked" answer: A -> 0.0.0.0, AAAA -> ::, anything else -> NOERROR with no records.
static size_t buildBlockResponse(const uint8_t* q, size_t qend, uint16_t qtype, uint8_t* out) {
  memcpy(out, q, qend);
  out[2] = 0x80 | (q[2] & 0x01);                   // QR=1, keep RD
  out[3] = 0x80;                                   // RA=1, RCODE=0
  out[4] = 0; out[5] = 1; out[6] = 0; out[7] = 0; out[8] = 0; out[9] = 0; out[10] = 0; out[11] = 0;
  size_t n = qend;
  if (qtype == 1 || qtype == 28) {
    out[7] = 1;                                    // ANCOUNT = 1
    uint16_t rdl = (qtype == 1) ? 4 : 16;
    const uint8_t rr[] = { 0xC0, 0x0C, 0x00, (uint8_t)qtype, 0x00, 0x01, 0, 0, 0, BLOCK_TTL, (uint8_t)(rdl >> 8), (uint8_t)rdl };
    memcpy(out + n, rr, sizeof rr); n += sizeof rr;
    memset(out + n, 0, rdl); n += rdl;
  }
  return n;
}

static void onClientPacket(AsyncUDPPacket& p) {
  uint8_t* d = p.data(); size_t len = p.length();
  char name[160]; uint16_t qtype = 0;
  size_t qend = parseQuery(d, len, name, sizeof(name), qtype);
  if (!qend) return;
  uint32_t ip = (uint32_t)p.remoteIP();
  uint32_t pu = pausedUntilMs;
  bool paused = pu && (int32_t)(millis() - pu) < 0;
  bool blocked = !paused && isBlocked(name);
  noteQuery(ip, name, qtype, blocked);

  if (blocked && qend <= 300) {
    uint8_t out[300 + 32];
    size_t n = buildBlockResponse(d, qend, qtype, out);
    p.write(out, n);
    return;
  }

  // Forward upstream under our own transaction id; remember who asked.
  uint16_t id;
  portENTER_CRITICAL(&pendMux);
  Pending* s = nullptr;
  for (int i = 0; i < MAX_PENDING; i++) if (!pend[i].used) { s = &pend[i]; break; }
  if (!s) {   // table full: reuse the oldest slot
    s = &pend[0];
    for (int i = 1; i < MAX_PENDING; i++) if ((int32_t)(pend[i].sentMs - s->sentMs) < 0) s = &pend[i];
    overflowDrops = overflowDrops + 1;
  }
  id = nextId++; if (nextId == 0) nextId = 1;
  s->used = true; s->tries = 1; s->ourId = id; s->origId = (d[0] << 8) | d[1];
  s->clientIp = ip; s->clientPort = p.remotePort(); s->sentMs = millis();
  s->len = (len <= PKT_MAX) ? (uint16_t)len : 0;
  if (s->len) { memcpy(s->pkt, d, len); s->pkt[0] = id >> 8; s->pkt[1] = id & 0xFF; }
  portEXIT_CRITICAL(&pendMux);

  d[0] = id >> 8; d[1] = id & 0xFF;
  up.writeTo(d, len, UPSTREAM_DNS_1, 53);
}

static void onUpstreamPacket(AsyncUDPPacket& p) {
  uint8_t* d = p.data(); size_t len = p.length();
  if (len < 12) return;
  uint16_t id = (d[0] << 8) | d[1];
  uint32_t cip = 0; uint16_t cport = 0, oid = 0; bool found = false;
  portENTER_CRITICAL(&pendMux);
  for (int i = 0; i < MAX_PENDING; i++)
    if (pend[i].used && pend[i].ourId == id) { found = true; cip = pend[i].clientIp; cport = pend[i].clientPort; oid = pend[i].origId; pend[i].used = false; break; }
  portEXIT_CRITICAL(&pendMux);
  if (!found) return;                              // late duplicate (both upstreams answered)
  d[0] = oid >> 8; d[1] = oid & 0xFF;
  srv.writeTo(d, len, IPAddress(cip), cport);
}

// Retry slow queries on the second upstream; drop dead ones.
static void sweepTask(void*) {
  static uint8_t copy[PKT_MAX];
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(250));
    uint32_t now = millis();
    for (int i = 0; i < MAX_PENDING; i++) {
      bool resend = false, drop = false; uint16_t len = 0;
      portENTER_CRITICAL(&pendMux);
      if (pend[i].used) {
        uint32_t age = now - pend[i].sentMs;
        if (pend[i].tries == 1 && age > RETRY_MS) {
          pend[i].tries = 2;
          if (pend[i].len) { memcpy(copy, pend[i].pkt, pend[i].len); len = pend[i].len; resend = true; }
        } else if (pend[i].tries >= 2 && age > DROP_MS) { pend[i].used = false; drop = true; }
      }
      portEXIT_CRITICAL(&pendMux);
      if (resend) up.writeTo(copy, len, UPSTREAM_DNS_2, 53);
      if (drop) timeouts = timeouts + 1;
    }
  }
}

// ---------------- blocklist: cache, fetch, swap ----------------
static void swapList(uint64_t* buf, size_t count, time_t fetched) {
  uint64_t* old;
  portENTER_CRITICAL(&listMux); old = list; list = buf; listCount = count; listFetched = fetched; portEXIT_CRITICAL(&listMux);
  if (old) { vTaskDelay(pdMS_TO_TICKS(250)); free(old); }   // any in-flight lookup is long done
}

static uint64_t* listAlloc(size_t entries) {
  if (psramFound()) return (uint64_t*)ps_malloc(entries * sizeof(uint64_t));
  if (entries <= LIST_CAP_INT) return (uint64_t*)malloc(entries * sizeof(uint64_t));
  return nullptr;
}

static bool loadCache() {
  File f = FFat.open(CACHE_FILE, FILE_READ);
  if (!f) return false;
  uint32_t magic = 0, ver = 0, count = 0; int64_t fetched = 0;
  bool ok = f.read((uint8_t*)&magic, 4) == 4 && f.read((uint8_t*)&ver, 4) == 4 &&
            f.read((uint8_t*)&count, 4) == 4 && f.read((uint8_t*)&fetched, 8) == 8;
  if (!ok || magic != CACHE_MAGIC || (ver != 1 && ver != 2) || count < MIN_LIST || count > LIST_CAP_PSRAM) { f.close(); return false; }
  bool sameUrls = false;
  if (ver == 2) { uint64_t uh = 0; sameUrls = (f.read((uint8_t*)&uh, 8) == 8) && (uh == urlsHash()); }
  uint64_t* buf = listAlloc(count);
  if (!buf) { f.close(); return false; }
  size_t need = count * sizeof(uint64_t), got = 0;
  while (got < need) {
    size_t r = f.read((uint8_t*)buf + got, min((size_t)4096, need - got));
    if (r == 0) break;
    got += r;
  }
  f.close();
  if (got != need) { free(buf); return false; }
  swapList(buf, count, (time_t)fetched);
  cacheStale = !sameUrls;
  if (cacheStale) Serial.println("[adblock] cached list was built from a different list config, will refetch");
  return true;
}

static bool saveCache(const uint64_t* buf, size_t count, time_t fetched) {
  FFat.remove(TEMP_FILE);
  File f = FFat.open(TEMP_FILE, FILE_WRITE);
  if (!f) return false;
  uint32_t magic = CACHE_MAGIC, ver = 2, c = (uint32_t)count; int64_t fe = (int64_t)fetched; uint64_t uh = urlsHash();
  f.write((uint8_t*)&magic, 4); f.write((uint8_t*)&ver, 4); f.write((uint8_t*)&c, 4); f.write((uint8_t*)&fe, 8); f.write((uint8_t*)&uh, 8);
  const uint8_t* p = (const uint8_t*)buf; size_t total = count * sizeof(uint64_t), put = 0;
  while (put < total) {
    size_t chunk = min((size_t)4096, total - put);
    if (f.write(p + put, chunk) != chunk) { f.close(); return false; }
    put += chunk;
    vTaskDelay(1);
  }
  f.close();
  FFat.remove(CACHE_FILE);
  return FFat.rename(TEMP_FILE, CACHE_FILE);
}

static size_t sortDedupe(uint64_t* buf, size_t n);
static void addDomain(uint64_t* buf, size_t cap, size_t& n, char* dom) {
  size_t L = strlen(dom);
  if (L < 3 || L > 150) return;
  for (size_t i = 0; i < L; i++) {
    char c = dom[i];
    if (c >= 'A' && c <= 'Z') dom[i] = c + 32;
    else if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_')) return;
  }
  if (!strchr(dom, '.')) return;                                   // localhost, local, broadcasthost
  if (!strcmp(dom, "localhost.localdomain") || !strncmp(dom, "ip6-", 4) || !strcmp(dom, "0.0.0.0")) return;
  if (n >= cap) { n = sortDedupe(buf, n); if (n >= cap) return; }   // full: squeeze out duplicates first
  buf[n++] = fnv1a(dom, L);
}

static void processLine(char* line, uint64_t* buf, size_t cap, size_t& n) {
  char* hash = strchr(line, '#'); if (hash) *hash = 0;
  char* save = nullptr;
  char* t1 = strtok_r(line, " \t\r", &save); if (!t1) return;
  if (t1[0] == '!' || t1[0] == '[') return;                        // adblock-syntax comments
  char* t2 = strtok_r(nullptr, " \t\r", &save);
  if (t2) { addDomain(buf, cap, n, t2); return; }                  // hosts format: "0.0.0.0 domain"
  char* d = t1;
  if (d[0] == '|' && d[1] == '|') d += 2;                          // AdGuard "||domain^"
  if (d[0] == '*' && d[1] == '.') d += 2;                          // wildcard "*.domain"
  char* caret = strchr(d, '^'); if (caret) *caret = 0;
  addDomain(buf, cap, n, d);
}

// Receives the download as it arrives and feeds it line by line to the parser, so
// HTTPClient can deal with Content-Length vs chunked transfer for us.
class LineSink : public Stream {
 public:
  LineSink(uint64_t* b, size_t c, size_t& n) : buf(b), cap(c), count(n) {}
  size_t write(uint8_t ch) override { feed((char)ch); bytes++; return 1; }
  size_t write(const uint8_t* d, size_t len) override { for (size_t i = 0; i < len; i++) feed((char)d[i]); bytes += len; return len; }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
  void finish() { if (ll) { line[ll] = 0; processLine(line, buf, cap, count); ll = 0; } }
  uint32_t bytes = 0;
 private:
  void feed(char c) { if (c == '\n') { line[ll] = 0; processLine(line, buf, cap, count); ll = 0; } else if (ll < sizeof(line) - 1) line[ll++] = c; }
  uint64_t* buf; size_t cap; size_t& count; char line[256]; size_t ll = 0;
};

static bool fetchOne(const char* url, uint64_t* buf, size_t cap, size_t& n, uint32_t& bytes) {
  WiFiClientSecure sec; sec.setInsecure();
  WiFiClient plain;
  NetworkClient& client = (strncmp(url, "https://", 8) == 0) ? (NetworkClient&)sec : (NetworkClient&)plain;
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(20000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setReuse(false);
  http.setUserAgent("netmon-esp32/1.0");
  if (!http.begin(client, url)) { snprintf(lastUpdateMsg, sizeof lastUpdateMsg, "bad list URL"); return false; }
  int code = http.GET();
  if (code != 200) { snprintf(lastUpdateMsg, sizeof lastUpdateMsg, "HTTP %d fetching list", code); http.end(); return false; }
  LineSink sink(buf, cap, n);
  int got = http.writeToStream(&sink);
  sink.finish();
  http.end();
  if (got < 0) { snprintf(lastUpdateMsg, sizeof lastUpdateMsg, "download error %d", got); return false; }
  bytes += sink.bytes;
  return true;
}

static size_t sortDedupe(uint64_t* buf, size_t n) {
  qsort(buf, n, sizeof(uint64_t), cmp64);
  size_t w = 0;
  for (size_t i = 0; i < n; i++) if (i == 0 || buf[i] != buf[i - 1]) buf[w++] = buf[i];
  return w;
}

static void fetchLists() {
  updating = true;
  uint32_t t0 = millis();
  size_t cap = psramFound() ? LIST_CAP_PSRAM : LIST_CAP_INT;
  uint64_t* buf = listAlloc(cap);
  if (!buf) { strcpy(lastUpdateMsg, "out of memory"); updating = false; return; }
  size_t n = 0; uint32_t bytes = 0; int okLists = 0, nLists = 0;
  for (int i = 0; BLOCKLIST_URLS[i]; i++) {
    nLists++;
    Serial.printf("[adblock] fetching %s\n", BLOCKLIST_URLS[i]);
    size_t before = n;
    if (fetchOne(BLOCKLIST_URLS[i], buf, cap, n, bytes)) {
      okLists++;
      size_t raw = n - before;
      n = sortDedupe(buf, n);
      Serial.printf("[adblock]   +%u entries, %u new -> %u unique so far%s\n", (unsigned)raw, (unsigned)(n - before), (unsigned)n, n >= cap ? " (CAP REACHED)" : "");
    }
    else Serial.printf("[adblock]   failed: %s\n", lastUpdateMsg);
  }
  if (okLists == 0 || n < MIN_LIST) {
    free(buf);
    if (okLists) snprintf(lastUpdateMsg, sizeof lastUpdateMsg, "list too small (%u entries)", (unsigned)n);
    Serial.printf("[adblock] update failed: %s\n", lastUpdateMsg);
    updating = false; return;
  }
  n = sortDedupe(buf, n);
  time_t now = epochNow();
  swapList(buf, n, now);                           // serve the new list now; this frees the old one
  uint64_t* fin = listAlloc(n);                    // then shrink to fit, with the old list's memory back
  if (fin) { memcpy(fin, buf, n * sizeof(uint64_t)); swapList(fin, n, now); buf = fin; }
  bool saved = saveCache(buf, n, now);
  cacheStale = false;
  snprintf(lastUpdateMsg, sizeof lastUpdateMsg, "%u domains from %d/%d lists, %u KB in %lus%s",
           (unsigned)n, okLists, nLists, (unsigned)(bytes / 1024), (unsigned long)((millis() - t0) / 1000), saved ? "" : " (cache save failed)");
  Serial.printf("[adblock] %s\n", lastUpdateMsg);
  Serial.printf("[adblock] psram %u KB free after update\n", (unsigned)(ESP.getFreePsram() / 1024));
  updating = false;
}

static void updateTask(void*) {
  vTaskDelay(pdMS_TO_TICKS(3000));
  for (;;) {
    size_t c; time_t f;
    portENTER_CRITICAL(&listMux); c = listCount; f = listFetched; portEXIT_CRITICAL(&listMux);
    time_t now = epochNow();
    bool stale = cacheStale || (c == 0) || (now && (!f || (now - f) > (time_t)REFRESH_DAYS * 86400));
    bool due = updateRequested || (stale && (lastAttemptMs == 0 || millis() - lastAttemptMs > 10UL * 60 * 1000));
    if (due && WiFi.status() == WL_CONNECTED) { updateRequested = false; lastAttemptMs = millis(); fetchLists(); }
    vTaskDelay(pdMS_TO_TICKS(30 * 1000));
  }
}

// ---------------- public API ----------------
void dnsblockBegin() {
  memset(&st, 0, sizeof st);
  memset(pend, 0, sizeof pend);
  for (int i = 0; ALLOWLIST[i] && allowN < 32; i++)   { String s = ALLOWLIST[i];   s.toLowerCase(); allowH[allowN++] = fnv1a(s.c_str(), s.length()); }
  for (int i = 0; EXTRA_BLOCK[i] && extraN < 32; i++) { String s = EXTRA_BLOCK[i]; s.toLowerCase(); extraH[extraN++] = fnv1a(s.c_str(), s.length()); }

  if (!FFat.begin(true)) Serial.println("[adblock] FFat mount failed: no cache, will fetch on every boot");
  else {
    Serial.printf("[adblock] FFat mounted, %u KB free\n", (unsigned)(FFat.freeBytes() / 1024));
    if (loadCache()) Serial.printf("[adblock] loaded %u cached domains\n", (unsigned)listCount);
    else Serial.println("[adblock] no cached list yet, will fetch");
  }
  Serial.printf("[adblock] psram %s, %u KB free\n", psramFound() ? "ok" : "MISSING", (unsigned)(ESP.getFreePsram() / 1024));

  if (!up.listen(UPSTREAM_PORT)) Serial.println("[adblock] upstream socket failed");
  up.onPacket(onUpstreamPacket);
  if (srv.listen(DNS_PORT)) { srv.onPacket(onClientPacket); Serial.printf("[adblock] DNS server listening on %s:53\n", WiFi.localIP().toString().c_str()); }
  else Serial.println("[adblock] FAILED to bind UDP 53");

  xTaskCreatePinnedToCore(sweepTask,  "dnssweep", 3072,  nullptr, 1, nullptr, 1);
  xTaskCreatePinnedToCore(updateTask, "dnsupd",   12288, nullptr, 1, nullptr, 1);
}

void dnsblockPause(uint32_t minutes) { uint32_t u = millis() + minutes * 60000UL; pausedUntilMs = u ? u : 1; }
void dnsblockResume()                 { pausedUntilMs = 0; }
void dnsblockRequestUpdate()          { updateRequested = true; }

String dnsblockJson() {
  portENTER_CRITICAL(&statMux); memcpy(&snap, &st, sizeof(Stats)); portEXIT_CRITICAL(&statMux);
  size_t lc; time_t lf;
  portENTER_CRITICAL(&listMux); lc = listCount; lf = listFetched; portEXIT_CRITICAL(&listMux);
  uint32_t now = millis(), pu = pausedUntilMs, to = timeouts, ov = overflowDrops;
  int32_t pausedLeft = pu ? (int32_t)(pu - now) : 0; if (pausedLeft < 0) pausedLeft = 0;
  bool upd = updating;

  String j; j.reserve(9000);
  j += "{\"paused_s\":" + String(pausedLeft / 1000);
  j += ",\"list_count\":" + String((unsigned)lc) + ",\"list_fetched\":" + String((long long)lf);
  j += ",\"updating\":"; j += upd ? "true" : "false";
  j += ",\"update_msg\":\"" + jsonEsc(lastUpdateMsg) + "\"";
  j += ",\"upstream\":[\"" + UPSTREAM_DNS_1.toString() + "\",\"" + UPSTREAM_DNS_2.toString() + "\"]";
  j += ",\"listen\":\"" + WiFi.localIP().toString() + "\"";
  j += ",\"psram_free_kb\":" + String((unsigned)(ESP.getFreePsram() / 1024)) + ",\"heap_free_kb\":" + String((unsigned)(ESP.getFreeHeap() / 1024));
  j += ",\"total\":" + String(snap.total) + ",\"blocked\":" + String(snap.blocked) + ",\"forwarded\":" + String(snap.forwarded)
     + ",\"timeouts\":" + String(to) + ",\"overflow\":" + String(ov);
  j += ",\"clients\":[";
  for (int i = 0; i < snap.nClients; i++) {
    if (i) j += ",";
    j += "{\"ip\":\"" + IPAddress(snap.clients[i].ip).toString() + "\",\"q\":" + String(snap.clients[i].q)
       + ",\"b\":" + String(snap.clients[i].b) + ",\"ago_s\":" + String((now - snap.clients[i].lastMs) / 1000) + "}";
  }
  j += "],\"recent\":[";
  for (int k = 0; k < snap.recentCount; k++) {
    int idx = (snap.recentHead - 1 - k + MAX_RECENT) % MAX_RECENT;
    Recent& r = snap.recent[idx];
    if (k) j += ",";
    j += "{\"t\":" + String(r.t) + ",\"ip\":\"" + IPAddress(r.ip).toString() + "\",\"name\":\"" + jsonEsc(r.name)
       + "\",\"type\":" + String((int)r.qtype) + ",\"blocked\":"; j += r.blocked ? "true" : "false"; j += "}";
  }
  j += "]}";
  return j;
}
