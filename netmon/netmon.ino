/*
  netmon — ESP32-S3 Internet Health Monitor + DNS ad blocker

  Every few seconds it probes your router and the internet, tracks latency,
  packet loss and outages, shows status on the onboard RGB LED, and serves a
  live dashboard on your LAN at http://netmon.local

  LED:  green = OK   amber = degraded   red = internet down (ISP)
        pink  = router unreachable      blue = connecting to Wi-Fi

  Board: ESP32-S3 (built-in USB-Serial/JTAG). No external libraries needed.
*/

#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "secrets.h"
#include "dnsblock.h"

// ---------------- Configuration ----------------
static const char*    HOSTNAME          = "netmon";   // -> http://netmon.local
static const uint32_t PROBE_INTERVAL_MS = 5000;       // time between probe rounds
static const int32_t  PROBE_TIMEOUT_MS  = 1500;       // per-probe TCP connect timeout
static const int      DEGRADED_RTT_MS   = 150;        // avg public RTT above this = degraded
static const int      HISTORY_LEN       = 180;        // 180 x 5 s = 15 min of samples
static const int      MAX_OUTAGES       = 10;         // outages remembered (newest kept)
static const int      DOWN_DEBOUNCE     = 2;          // bad rounds in a row before "outage"
static const uint8_t  LED_BRIGHT        = 24;         // 0-255, keep it gentle
static const char*    TZ_INFO = "GMT0BST,M3.5.0/1,M10.5.0";   // Europe/London
#ifndef RGB_BUILTIN
  #define RGB_BUILTIN 48                              // onboard WS2812 on most S3 devkits
#endif

// ---------------- Probe targets ----------------
// A TCP connect to a known-open port measures one round trip, like a ping,
// but needs no ICMP library and proves a real service is answering.
struct Target { const char* name; IPAddress ip; uint16_t port; bool isGateway; };
static Target targets[] = {
  { "Router",     IPAddress(0, 0, 0, 0), 443, true  },   // filled in from DHCP gateway
  { "Cloudflare", IPAddress(1, 1, 1, 1),  53, false },
  { "Google",     IPAddress(8, 8, 8, 8),  53, false },
};
static const int NT = sizeof(targets) / sizeof(targets[0]);

// ---------------- State ----------------
enum NetState : uint8_t { ST_CONNECTING = 0, ST_UP, ST_DEGRADED, ST_DOWN_ISP, ST_DOWN_LAN };
static const char* STATE_NAMES[] = { "connecting", "up", "degraded", "down_isp", "down_lan" };
static const char* STATE_TEXT[]  = {
  "Connecting to Wi-Fi",
  "Internet OK",
  "Internet degraded",
  "Internet down — router is reachable, so it's likely the ISP",
  "Router unreachable — Wi-Fi or router problem",
};

struct Sample { int16_t rtt[NT]; int16_t dns; };
static Sample   hist[HISTORY_LEN];
static int      histHead = 0, histCount = 0;

struct Outage { time_t start; time_t end; uint8_t kind; };   // end == 0 -> ongoing
static Outage   outages[MAX_OUTAGES];
static int      outageCount = 0;

static volatile NetState state = ST_CONNECTING;
static int      lastRtt[NT];
static int      lastDns = -1;
static uint32_t okTotal[NT], failTotal[NT];
static uint32_t rounds = 0;
static int      downStreak = 0;
static bool     inOutage = false;
static time_t   firstDownEpoch = 0;
static time_t   bootEpoch = 0;
static SemaphoreHandle_t mtx;
static WebServer server(80);

// ---------------- Helpers ----------------
static time_t nowEpoch() { time_t t = time(nullptr); return (t > 1600000000) ? t : 0; }

static void led(uint8_t r, uint8_t g, uint8_t b) {
  rgbLedWrite(RGB_BUILTIN, (uint8_t)(r * LED_BRIGHT / 255), (uint8_t)(g * LED_BRIGHT / 255), (uint8_t)(b * LED_BRIGHT / 255));
}
static void ledForState(NetState s) {
  switch (s) {
    case ST_UP:       led(0, 255, 0);    break;
    case ST_DEGRADED: led(255, 140, 0);  break;
    case ST_DOWN_ISP: led(255, 0, 0);    break;
    case ST_DOWN_LAN: led(255, 0, 80);   break;
    default:          led(0, 0, 255);    break;
  }
}

static String fmtDur(long s) {
  if (s < 60) return String(s) + "s";
  if (s < 3600) return String(s / 60) + "m " + String(s % 60) + "s";
  return String(s / 3600) + "h " + String((s % 3600) / 60) + "m";
}

static String jsonEscape(const String& s) {
  String o; o.reserve(s.length() + 4);
  for (size_t i = 0; i < s.length(); i++) { char c = s[i]; if (c == '"' || c == '\\') o += '\\'; o += c; }
  return o;
}

// Round-trip time of a TCP connect, in ms. -1 on failure.
static int tcpRtt(const IPAddress& ip, uint16_t port) {
  WiFiClient c;
  uint32_t t0 = millis();
  bool ok = c.connect(ip, port, PROBE_TIMEOUT_MS);
  uint32_t dt = millis() - t0;
  c.stop();
  return ok ? (int)dt : -1;
}

// DNS resolution time, in ms. -1 on failure. (May be cached -> ~0 ms; still proves DNS works.)
static int dnsRtt() {
  IPAddress ip;
  uint32_t t0 = millis();
  int ok = WiFi.hostByName("one.one.one.one", ip);
  uint32_t dt = millis() - t0;
  return (ok == 1) ? (int)dt : -1;
}

// Phone notification via ntfy.sh (plain HTTP). No-op if NTFY_TOPIC is empty.
static void notify(const char* title, const String& msg) {
  if (strlen(NTFY_TOPIC) == 0) return;
  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(3000);
  http.begin(String("http://ntfy.sh/") + NTFY_TOPIC);
  http.addHeader("Title", title);
  http.addHeader("Tags", "satellite");
  int code = http.POST(msg);
  Serial.printf("[ntfy] %s -> HTTP %d\n", title, code);
  http.end();
}

// ---------------- Probe task (runs on core 0, web server stays responsive on core 1) ----------------
static void probeTask(void*) {
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      xSemaphoreTake(mtx, portMAX_DELAY);
      state = ST_CONNECTING;
      xSemaphoreGive(mtx);
      ledForState(ST_CONNECTING);
      Serial.println("[wifi] disconnected, reconnecting...");
      WiFi.reconnect();
      vTaskDelay(pdMS_TO_TICKS(3000));
      continue;
    }

    uint32_t roundStart = millis();
    int rtt[NT];
    for (int i = 0; i < NT; i++) rtt[i] = tcpRtt(targets[i].ip, targets[i].port);
    int dns = dnsRtt();

    // ---- classify this round ----
    bool gwOk = false, anyPubOk = false, allPubOk = true;
    long sum = 0; int n = 0;
    for (int i = 0; i < NT; i++) {
      bool ok = rtt[i] >= 0;
      if (targets[i].isGateway) { gwOk = ok; }
      else { anyPubOk |= ok; allPubOk &= ok; if (ok) { sum += rtt[i]; n++; } }
    }
    int pubAvg = n ? (int)(sum / n) : -1;

    NetState s;
    if (!gwOk && !anyPubOk)  s = ST_DOWN_LAN;
    else if (!anyPubOk)      s = ST_DOWN_ISP;
    else if (!allPubOk || !gwOk || dns < 0 || pubAvg > DEGRADED_RTT_MS) s = ST_DEGRADED;
    else                     s = ST_UP;
    bool isDown = (s == ST_DOWN_ISP || s == ST_DOWN_LAN);

    // ---- record ----
    bool startedOutage = false, endedOutage = false; long outDur = 0; uint8_t outKind = 0;
    xSemaphoreTake(mtx, portMAX_DELAY);
    for (int i = 0; i < NT; i++) { lastRtt[i] = rtt[i]; if (rtt[i] >= 0) okTotal[i]++; else failTotal[i]++; }
    lastDns = dns;
    Sample& sm = hist[histHead];
    for (int i = 0; i < NT; i++) sm.rtt[i] = (rtt[i] < 0) ? -1 : (int16_t)min(rtt[i], 32000);
    sm.dns = (dns < 0) ? -1 : (int16_t)min(dns, 32000);
    histHead = (histHead + 1) % HISTORY_LEN;
    if (histCount < HISTORY_LEN) histCount++;
    rounds++;

    if (isDown) {
      downStreak++;
      if (downStreak == 1) firstDownEpoch = nowEpoch();
      if (!inOutage && downStreak >= DOWN_DEBOUNCE) {
        inOutage = true; startedOutage = true;
        if (outageCount == MAX_OUTAGES) { memmove(&outages[0], &outages[1], sizeof(Outage) * (MAX_OUTAGES - 1)); outageCount--; }
        outages[outageCount++] = { firstDownEpoch, 0, (uint8_t)s };
      }
    } else {
      downStreak = 0;
      if (inOutage) {
        inOutage = false; endedOutage = true;
        Outage& o = outages[outageCount - 1];
        o.end = nowEpoch();
        outDur = (o.start && o.end) ? (long)(o.end - o.start) : 0;
        outKind = o.kind;
      }
    }
    state = s;
    xSemaphoreGive(mtx);

    // ---- report ----
    ledForState(s);
    Serial.printf("[probe] %-18s | ", STATE_NAMES[s]);
    for (int i = 0; i < NT; i++) Serial.printf("%s=%d ", targets[i].name, rtt[i]);
    Serial.printf("dns=%d  (ms, -1 = fail)\n", dns);

    if (startedOutage) Serial.println("[outage] STARTED");
    if (endedOutage) {
      Serial.printf("[outage] ENDED after %s\n", fmtDur(outDur).c_str());
      notify("Internet is back", "Down for " + fmtDur(outDur) +
             (outKind == ST_DOWN_LAN ? " (router was unreachable)" : " (router was up — ISP/upstream issue)"));
    }

    uint32_t spent = millis() - roundStart;
    if (spent < PROBE_INTERVAL_MS) vTaskDelay(pdMS_TO_TICKS(PROBE_INTERVAL_MS - spent));
  }
}

// ---------------- JSON API ----------------
static String statusJson() {
  String j; j.reserve(1800);
  xSemaphoreTake(mtx, portMAX_DELAY);
  NetState s = state;
  j += "{\"state\":\""; j += STATE_NAMES[s]; j += "\",\"text\":\""; j += STATE_TEXT[s]; j += "\"";
  j += ",\"uptime_s\":" + String(millis() / 1000);
  j += ",\"now\":"  + String((long long)nowEpoch());
  j += ",\"boot\":" + String((long long)bootEpoch);
  j += ",\"rounds\":" + String(rounds);
  j += ",\"interval_ms\":" + String(PROBE_INTERVAL_MS);
  j += ",\"in_outage\":"; j += (inOutage ? "true" : "false");
  j += ",\"wifi\":{\"ssid\":\"" + jsonEscape(WiFi.SSID()) + "\",\"rssi\":" + String((int)WiFi.RSSI())
     + ",\"ip\":\"" + WiFi.localIP().toString() + "\",\"gw\":\"" + WiFi.gatewayIP().toString()
     + "\",\"host\":\"" + String(HOSTNAME) + ".local\"}";
  j += ",\"targets\":[";
  for (int i = 0; i < NT; i++) {
    long sum = 0; int ok = 0, fail = 0;
    for (int k = 0; k < histCount; k++) {
      int idx = (histHead - histCount + k + HISTORY_LEN) % HISTORY_LEN;
      int v = hist[idx].rtt[i];
      if (v >= 0) { sum += v; ok++; } else fail++;
    }
    int avg = ok ? (int)(sum / ok) : -1;
    int lossPct = (ok + fail) ? (fail * 100) / (ok + fail) : 0;
    if (i) j += ",";
    j += "{\"name\":\"" + String(targets[i].name) + "\",\"host\":\"" + targets[i].ip.toString() + ":" + String((int)targets[i].port)
       + "\",\"rtt\":" + String(lastRtt[i]) + ",\"avg\":" + String(avg) + ",\"loss\":" + String(lossPct)
       + ",\"ok\":" + String(okTotal[i]) + ",\"fail\":" + String(failTotal[i])
       + ",\"gateway\":"; j += (targets[i].isGateway ? "true" : "false"); j += "}";
  }
  j += "],\"dns\":{\"rtt\":" + String(lastDns) + "}";
  j += ",\"outages\":[";
  for (int i = outageCount - 1; i >= 0; i--) {
    if (i != outageCount - 1) j += ",";
    j += "{\"start\":" + String((long long)outages[i].start) + ",\"end\":" + String((long long)outages[i].end)
       + ",\"kind\":\"" + String(STATE_NAMES[outages[i].kind]) + "\"}";
  }
  j += "]}";
  xSemaphoreGive(mtx);
  return j;
}

static String historyJson() {
  String j; j.reserve(HISTORY_LEN * (NT + 1) * 6 + 200);
  xSemaphoreTake(mtx, portMAX_DELAY);
  j += "{\"interval_ms\":" + String(PROBE_INTERVAL_MS) + ",\"count\":" + String(histCount) + ",\"names\":[";
  for (int i = 0; i < NT; i++) { if (i) j += ","; j += "\"" + String(targets[i].name) + "\""; }
  j += "],\"rtt\":[";
  for (int i = 0; i < NT; i++) {
    if (i) j += ",";
    j += "[";
    for (int k = 0; k < histCount; k++) {
      int idx = (histHead - histCount + k + HISTORY_LEN) % HISTORY_LEN;
      if (k) j += ",";
      j += String((int)hist[idx].rtt[i]);
    }
    j += "]";
  }
  j += "],\"dns\":[";
  for (int k = 0; k < histCount; k++) {
    int idx = (histHead - histCount + k + HISTORY_LEN) % HISTORY_LEN;
    if (k) j += ",";
    j += String((int)hist[idx].dns);
  }
  j += "]}";
  xSemaphoreGive(mtx);
  return j;
}

// ---------------- Dashboard (served from flash) ----------------
static const char INDEX_HTML[] PROGMEM = R"rawliteral(<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>netmon</title>
<style>
:root{--bg:#0b0f14;--card:#121821;--line:#1f2933;--fg:#e6edf3;--mut:#8b98a5;--ok:#2ecc71;--warn:#f39c12;--bad:#e74c3c;--info:#3b82f6}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.4 -apple-system,system-ui,"Segoe UI",Roboto,sans-serif}
.wrap{max-width:960px;margin:0 auto;padding:20px}
header{display:flex;align-items:center;gap:14px;flex-wrap:wrap;margin-bottom:18px}
.dot{width:16px;height:16px;border-radius:50%;background:var(--info);box-shadow:0 0 12px var(--info)}
h1{font-size:22px;margin:0}.sub{color:var(--mut);font-size:13px;width:100%}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(210px,1fr));gap:12px}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px}
.card h3{margin:0 0 4px;font-size:12px;color:var(--mut);font-weight:600;text-transform:uppercase;letter-spacing:.04em}
.big{font-size:32px;font-weight:700;line-height:1.1}.big small{font-size:14px;color:var(--mut);font-weight:500}
.meta{color:var(--mut);font-size:12px;margin-top:4px}canvas{width:100%;height:44px;display:block;margin-top:10px}
h2{font-size:13px;color:var(--mut);margin:22px 0 8px;text-transform:uppercase;letter-spacing:.04em}
table{width:100%;border-collapse:collapse;font-size:13px}td,th{padding:8px 6px;border-bottom:1px solid var(--line);text-align:left}th{color:var(--mut);font-weight:600}
tr:last-child td{border-bottom:0}
.pill{display:inline-block;padding:2px 8px;border-radius:99px;font-size:12px;font-weight:600;background:var(--bad);color:#fff}
.up .dot{background:var(--ok);box-shadow:0 0 12px var(--ok)}.degraded .dot{background:var(--warn);box-shadow:0 0 12px var(--warn)}
.down_isp .dot,.down_lan .dot{background:var(--bad);box-shadow:0 0 12px var(--bad)}
.ok{color:var(--ok)}.warn{color:var(--warn)}.bad{color:var(--bad)}
footer{color:var(--mut);font-size:12px;margin-top:22px}
.pill.on{background:var(--ok);color:#06210f}.pill.warn{background:var(--warn);color:#2a1800}
.btn{background:#1f2933;color:var(--fg);border:1px solid #2b3742;border-radius:8px;padding:6px 12px;font-size:13px;cursor:pointer}.btn:hover{background:#2b3742}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin:8px 0 12px}
.two{display:grid;grid-template-columns:1fr 1.7fr;gap:12px;margin-top:12px}@media(max-width:760px){.two{grid-template-columns:1fr}}
.scroll{overflow:auto;max-height:420px}td.dom{font-family:ui-monospace,Menlo,monospace;font-size:12px;word-break:break-all}
</style></head><body><div class="wrap">
<header id="hdr"><div class="dot"></div><h1 id="status">Loading…</h1><div class="sub" id="sub"></div></header>
<h2>Ad blocker</h2>
<div class="row"><span id="abstate"></span>
 <button class="btn" onclick="act('/api/pause?min=5')">Pause 5 min</button>
 <button class="btn" onclick="act('/api/pause?min=60')">Pause 1 hour</button>
 <button class="btn" onclick="act('/api/resume')">Resume</button>
 <button class="btn" onclick="act('/api/update')">Update list</button></div>
<div class="sub" id="absub" style="margin-bottom:12px"></div>
<div class="grid" id="abstats"></div>
<div class="two">
 <div class="card scroll"><h3>Devices</h3><table><thead><tr><th>Client</th><th>Queries</th><th>Blocked</th><th>Last seen</th></tr></thead><tbody id="clients"></tbody></table></div>
 <div class="card scroll"><h3>Recent queries</h3><table><thead><tr><th>Time</th><th>Client</th><th>Domain</th><th>Type</th><th></th></tr></thead><tbody id="recent"></tbody></table></div>
</div>
<h2>Internet</h2>
<div class="grid" id="cards"></div>
<h2>Outages</h2>
<div class="card"><table><thead><tr><th>Started</th><th>Ended</th><th>Duration</th><th>Cause</th></tr></thead><tbody id="outages"></tbody></table></div>
<footer id="foot"></footer>
</div>
<script>
const $=s=>document.querySelector(s);
const fmtT=e=>e?new Date(e*1000).toLocaleString('en-GB',{hour12:false}):'—';
const fmtD=s=>{if(s==null||s<0)return'—';if(s<60)return s+'s';if(s<3600)return Math.floor(s/60)+'m '+(s%60)+'s';const h=Math.floor(s/3600);return h+'h '+Math.floor(s%3600/60)+'m'};
const cls=r=>r<0?'bad':r>150?'warn':'ok';
const QT={1:'A',28:'AAAA',65:'HTTPS',64:'SVCB',12:'PTR',16:'TXT',33:'SRV',5:'CNAME',15:'MX',2:'NS',6:'SOA'};
const ago=s=>s<60?s+'s ago':s<3600?Math.floor(s/60)+'m ago':s<86400?Math.floor(s/3600)+'h ago':Math.floor(s/86400)+'d ago';
async function act(u){try{await fetch(u,{method:'POST'})}catch(e){}tick();}
function renderDns(d,nowE){
 const pct=d.total?Math.round(d.blocked*100/d.total):0, paused=d.paused_s>0;
 $('#abstate').innerHTML=paused?`<span class="pill">PAUSED · ${fmtD(d.paused_s)} left</span>`:d.list_count?`<span class="pill on">BLOCKING</span>`:`<span class="pill warn">NO LIST YET · forwarding only</span>`;
 $('#absub').textContent=`${d.list_count.toLocaleString()} domains · ${d.list_fetched&&nowE?'list updated '+ago(nowE-d.list_fetched):'list not fetched yet'} · ${d.updating?'updating now…':d.update_msg} · DNS server address ${d.listen} · upstream ${d.upstream.join(' / ')}`;
 $('#abstats').innerHTML=`<div class="card"><h3>Queries</h3><div class="big">${d.total.toLocaleString()}</div><div class="meta">since boot</div></div>
  <div class="card"><h3>Blocked</h3><div class="big bad">${d.blocked.toLocaleString()}</div><div class="meta">${pct}% of queries</div></div>
  <div class="card"><h3>Forwarded</h3><div class="big ok">${d.forwarded.toLocaleString()}</div><div class="meta">${d.timeouts} upstream timeouts</div></div>
  <div class="card"><h3>Devices</h3><div class="big">${d.clients.length}</div><div class="meta">using this DNS</div></div>`;
 $('#clients').innerHTML=d.clients.length?d.clients.sort((a,b)=>b.q-a.q).map(c=>`<tr><td>${c.ip}</td><td>${c.q}</td><td class="bad">${c.b}</td><td>${ago(c.ago_s)}</td></tr>`).join(''):`<tr><td colspan="4">Nothing yet. Set the router's DNS server to ${d.listen}, or set it on one device to try.</td></tr>`;
 $('#recent').innerHTML=d.recent.length?d.recent.map(r=>`<tr><td>${r.t?new Date(r.t*1000).toLocaleTimeString('en-GB'):'—'}</td><td>${r.ip}</td><td class="dom ${r.blocked?'bad':''}">${r.name}</td><td>${QT[r.type]||r.type}</td><td>${r.blocked?'<span class="pill">blocked</span>':''}</td></tr>`).join(''):'<tr><td colspan="5">—</td></tr>';
}
function spark(cv,arr){const dpr=devicePixelRatio||1,w=cv.clientWidth,h=cv.clientHeight;cv.width=w*dpr;cv.height=h*dpr;
 const c=cv.getContext('2d');c.scale(dpr,dpr);c.clearRect(0,0,w,h);if(!arr||!arr.length)return;
 const vals=arr.filter(v=>v>=0);const max=Math.max(50,...vals);const n=arr.length,sx=w/Math.max(1,n-1);
 c.lineWidth=1.5;c.strokeStyle='#3b82f6';c.beginPath();let pen=false;
 arr.forEach((v,i)=>{const x=i*sx;if(v<0){pen=false;c.fillStyle='#e74c3c';c.fillRect(x-1,0,2,h);return;}
  const y=h-2-(v/max)*(h-4);if(!pen){c.moveTo(x,y);pen=true}else c.lineTo(x,y)});c.stroke();}
async function tick(){try{
 const s=await fetch('/api/status').then(r=>r.json());
 const hst=await fetch('/api/history').then(r=>r.json());
 const dn=await fetch('/api/dns').then(r=>r.json());
 renderDns(dn,s.now);
 $('#hdr').className=s.state;$('#status').textContent=s.text;
 $('#sub').textContent=`${s.wifi.ssid} · ${s.wifi.ip} · gateway ${s.wifi.gw} · Wi-Fi ${s.wifi.rssi} dBm · probing every ${s.interval_ms/1000}s`;
 let html='';
 s.targets.forEach((t,i)=>{html+=`<div class="card"><h3>${t.name}</h3><div class="big ${cls(t.rtt)}">${t.rtt<0?'✕':t.rtt+'<small> ms</small>'}</div><div class="meta">avg ${t.avg<0?'—':t.avg+' ms'} · loss ${t.loss}% over 15 min · ${t.host}</div><canvas id="c${i}"></canvas></div>`});
 html+=`<div class="card"><h3>DNS</h3><div class="big ${cls(s.dns.rtt)}">${s.dns.rtt<0?'✕':s.dns.rtt+'<small> ms</small>'}</div><div class="meta">resolving one.one.one.one</div><canvas id="cd"></canvas></div>`;
 $('#cards').innerHTML=html;
 hst.rtt.forEach((a,i)=>spark($('#c'+i),a));spark($('#cd'),hst.dns);
 $('#outages').innerHTML=s.outages.length?s.outages.map(o=>`<tr><td>${fmtT(o.start)}</td><td>${o.end?fmtT(o.end):'<span class="pill">ongoing</span>'}</td><td>${o.end?fmtD(o.end-o.start):fmtD(s.now-o.start)}</td><td>${o.kind==='down_lan'?'Router unreachable':'ISP / upstream'}</td></tr>`).join(''):'<tr><td colspan="4" class="ok">No outages recorded since boot</td></tr>';
 $('#foot').textContent=`netmon + ad blocker on ESP32-S3 · up ${fmtD(s.uptime_s)} · booted ${fmtT(s.boot)} · ${s.rounds} probe rounds · ${s.now?'clock synced':'clock not synced yet'}`;
}catch(e){$('#status').textContent='Cannot reach netmon';$('#hdr').className='down_lan'}}
tick();setInterval(tick,5000);
</script></body></html>)rawliteral";

// ---------------- Setup / loop ----------------
void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);   // never stall if no USB terminal is attached
#endif
  delay(300);
  Serial.println("\n[netmon] booting");
  Serial.printf("[mem] heap %u KB free, psram %u KB\n", (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(ESP.getPsramSize() / 1024));

  mtx = xSemaphoreCreateMutex();
  for (int i = 0; i < NT; i++) { lastRtt[i] = -1; okTotal[i] = failTotal[i] = 0; }
  led(0, 0, 255);

  WiFi.persistent(false);
  WiFi.setHostname(HOSTNAME);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  if (strlen(STATIC_IP)) {
    IPAddress ip, gw, mask;
    if (ip.fromString(STATIC_IP) && gw.fromString(STATIC_GW) && mask.fromString(STATIC_MASK)) {
      WiFi.config(ip, gw, mask, IPAddress(1, 1, 1, 1), IPAddress(8, 8, 8, 8));
      Serial.printf("[wifi] using fixed address %s\n", STATIC_IP);
    }
  }
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("[wifi] connecting to \"%s\" ", WIFI_SSID);

  bool on = false; uint32_t lastReport = millis();
  while (WiFi.status() != WL_CONNECTED) {
    on = !on; led(0, 0, on ? 255 : 0); delay(400); Serial.print(".");
    if (millis() - lastReport > 15000) {
      lastReport = millis();
      Serial.printf("\n[wifi] still connecting, status=%d (1=SSID not found, 4=connect failed/bad password, 6=disconnected)\n", (int)WiFi.status());
    }
  }
  Serial.printf("\n[wifi] connected  ip=%s  gw=%s  rssi=%d dBm\n",
                WiFi.localIP().toString().c_str(), WiFi.gatewayIP().toString().c_str(), (int)WiFi.RSSI());
  for (int i = 0; i < NT; i++) if (targets[i].isGateway) targets[i].ip = WiFi.gatewayIP();

  configTzTime(TZ_INFO, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  for (int i = 0; i < 40 && nowEpoch() == 0; i++) delay(100);
  bootEpoch = nowEpoch();
  Serial.printf("[time] %s\n", bootEpoch ? "synced" : "not synced yet (will keep trying)");

  if (MDNS.begin(HOSTNAME)) { MDNS.addService("http", "tcp", 80); Serial.printf("[mdns] http://%s.local\n", HOSTNAME); }

  dnsblockBegin();

  server.on("/",            []() { server.send_P(200, "text/html", INDEX_HTML); });
  server.on("/api/dns",     HTTP_GET,  []() { server.send(200, "application/json", dnsblockJson()); });
  server.on("/api/pause",   HTTP_POST, []() { int m = server.arg("min").toInt(); dnsblockPause(m > 0 ? m : 5); server.send(200, "application/json", "{\"ok\":true}"); });
  server.on("/api/resume",  HTTP_POST, []() { dnsblockResume(); server.send(200, "application/json", "{\"ok\":true}"); });
  server.on("/api/update",  HTTP_POST, []() { dnsblockRequestUpdate(); server.send(200, "application/json", "{\"ok\":true}"); });
  server.on("/api/status",  []() { server.send(200, "application/json", statusJson()); });
  server.on("/api/history", []() { server.send(200, "application/json", historyJson()); });
  server.onNotFound(        []() { server.send(404, "text/plain", "not found"); });
  server.begin();
  Serial.printf("[http] dashboard at http://%s/  and  http://%s.local/\n", WiFi.localIP().toString().c_str(), HOSTNAME);

  xTaskCreatePinnedToCore(probeTask, "probe", 8192, nullptr, 1, nullptr, 0);
  notify("netmon online", "Monitoring " + String(WIFI_SSID) + " — dashboard at http://" + HOSTNAME + ".local");
}

void loop() {
  server.handleClient();
  delay(2);
}
