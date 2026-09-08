#pragma once
#include <Arduino.h>
// The two web pages the board serves, kept out of netmon.ino on purpose: the Arduino build tool scans the
// sketch for C++ function definitions and trips over JavaScript inside a raw string. Headers are not scanned.

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
a{color:var(--info)}.inp{background:#0b0f14;color:var(--fg);border:1px solid #2b3742;border-radius:8px;padding:6px 10px;font-size:13px;min-width:170px}.lst{list-style:none;margin:8px 0 0;padding:0;font-size:13px}.lst li{display:flex;justify-content:space-between;gap:8px;padding:4px 0;border-bottom:1px solid var(--line);font-family:ui-monospace,Menlo,monospace;font-size:12px}.x{cursor:pointer;color:var(--mut)}.x:hover{color:var(--bad)}.btn.sm{padding:2px 8px;font-size:12px}.nm{cursor:pointer;border-bottom:1px dashed var(--mut)}.scroll{overflow:auto;max-height:420px}td.dom{font-family:ui-monospace,Menlo,monospace;font-size:12px;word-break:break-all}
</style></head><body><div class="wrap">
<header id="hdr"><div class="dot"></div><h1 id="status">Loading…</h1><div class="sub" id="sub"></div></header>
<h2>Boards</h2>
<div class="grid" id="boards"></div>
<h2>Ad blocker</h2>
<div class="row"><span id="abstate"></span>
 <button class="btn" onclick="act('/api/pause?min=5')">Pause 5 min</button>
 <button class="btn" onclick="act('/api/pause?min=60')">Pause 1 hour</button>
 <button class="btn" onclick="act('/api/resume')">Resume</button>
 <button class="btn" onclick="act('/api/update')">Update list</button></div>
<div class="sub" id="absub" style="margin-bottom:12px"></div>
<div class="grid" id="abstats"></div>
<div class="two">
 <div class="card scroll"><h3>Devices <span class="meta">· click a name to change it</span></h3><table><thead><tr><th>Device</th><th>Queries</th><th>Blocked</th><th>YouTube ext.</th><th>Last seen</th><th></th></tr></thead><tbody id="clients"></tbody></table></div>
 <div class="card scroll"><h3>Recent queries</h3><table><thead><tr><th>Time</th><th>Client</th><th>Domain</th><th>Type</th><th></th></tr></thead><tbody id="recent"></tbody></table></div>
</div>
<h2>YouTube ads (browser extension)</h2>
<div class="row" id="ytenforce"></div>
<div class="sub" id="ytsub" style="margin-bottom:12px"></div>
<div class="grid" id="ytstats"></div>
<div class="sub" id="ytmissing" style="margin-top:10px"></div>
<h2>Settings <span style="text-transform:none;letter-spacing:0">· saved on the board, no reflash needed</span></h2>
<div class="grid">
 <div class="card"><h3>Always allow</h3><div class="meta">Never block these sites (and their subdomains), even if a list has them.</div><ul class="lst" id="l_allow"></ul><div class="row"><input class="inp" id="i_allow" placeholder="example.com"><button class="btn" onclick="addTo('allow')">Add</button></div></div>
 <div class="card"><h3>Always block</h3><div class="meta">Block these on top of the lists.</div><ul class="lst" id="l_block"></ul><div class="row"><input class="inp" id="i_block" placeholder="annoying-site.com"><button class="btn" onclick="addTo('block')">Add</button></div></div>
 <div class="card"><h3>Exempt from the YouTube rule</h3><div class="meta">Phones and TVs: never held back when "Require the extension" is on.</div><ul class="lst" id="l_exempt"></ul><div class="row"><input class="inp" id="i_exempt" placeholder="192.168.1.151"><button class="btn" onclick="addTo('exempt')">Add</button></div></div>
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
const esc=s=>String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
// One dashboard for every board: this one plus the peers it lists in /api/status. Reads are merged, writes go to all.
let BOARDS=[location.origin], NAMES={};
const getJ=(b,p)=>fetch(b+p,{cache:'no-store'}).then(r=>{if(!r.ok)throw 0;return r.json()});
const fromAll=p=>Promise.allSettled(BOARDS.map(b=>getJ(b,p)));
async function act(u){await Promise.allSettled(BOARDS.map(b=>fetch(b+u,{method:'POST'})));tick();}
async function one(b,u){try{await fetch(b+u,{method:'POST'})}catch(e){}tick();}
const label=ip=>NAMES[ip]?`<span class="nm" title="${esc(ip)} · click to rename" onclick="rename('${ip}')">${esc(NAMES[ip])}</span> <span class="meta">${ip}</span>`:`<span class="nm" title="click to name this device" onclick="rename('${ip}')">${ip}</span>`;
async function rename(ip){const n=prompt('Name for '+ip+(NAMES[ip]?' (empty removes the name)':''),NAMES[ip]||'');if(n===null)return;await act('/api/device?ip='+ip+'&name='+encodeURIComponent(n));}
async function addTo(list){const i=$('#i_'+list),v=i.value.trim();if(!v)return;const u='/api/lists?list='+list+'&add='+encodeURIComponent(v);const r=await fetch(u,{method:'POST'}).then(r=>r.json()).catch(()=>({ok:false,error:'no answer'}));if(!r.ok){alert(r.error||'rejected');return}i.value='';await Promise.allSettled(BOARDS.slice(1).map(b=>fetch(b+u,{method:'POST'})));tick();}
async function rmFrom(list,v){await act('/api/lists?list='+list+'&remove='+encodeURIComponent(v));}
function renderLists(L){NAMES={};(L.names||[]).forEach(n=>NAMES[n.ip]=n.name);
 for(const k of ['allow','block','exempt']){const c=L.compiled?.[k]||0,arr=L[k]||[];$('#l_'+k).innerHTML=(arr.length?arr.map(v=>`<li><span>${esc(v)}</span><span class="x" onclick="rmFrom('${k}','${esc(v)}')">✕</span></li>`).join(''):'<li><span class="meta">nothing added here</span></li>')+(c?`<li><span class="meta">+ ${c} built into the firmware (dnsconfig.h)</span></li>`:'')}}
const minPos=(a,b)=>a<0?b:b<0?a:Math.min(a,b);
function mergeDns(ds){const d={...ds[0]},cl={};
 ds.forEach(x=>x.clients.forEach(c=>{const m=cl[c.ip]||(cl[c.ip]={ip:c.ip,q:0,b:0,ago_s:1e9,yt_ago_s:-1,ext_ago_s:-1,paused_s:0,name:''});m.q+=c.q;m.b+=c.b;m.ago_s=Math.min(m.ago_s,c.ago_s);m.yt_ago_s=minPos(m.yt_ago_s,c.yt_ago_s);m.ext_ago_s=minPos(m.ext_ago_s,c.ext_ago_s);m.paused_s=Math.max(m.paused_s,c.paused_s);m.name=m.name||c.name;}));
 d.clients=Object.values(cl);for(const k of ['total','blocked','forwarded','timeouts'])d[k]=ds.reduce((a,x)=>a+(x[k]||0),0);
 d.recent=ds.flatMap(x=>x.recent).sort((a,b)=>b.t-a.t).slice(0,60);d.list_count=Math.max(...ds.map(x=>x.list_count));d.paused_s=Math.max(...ds.map(x=>x.paused_s));d.updating=ds.some(x=>x.updating);d.listen=ds.map(x=>x.listen).join(' + ');d.off=ds.filter(x=>x.enabled===false).map(x=>x.listen);return d;}
function mergeYt(ys){const y={...ys[0]},dv={};ys.forEach(x=>(x.devices||[]).forEach(v=>{const m=dv[v.ip]||(dv[v.ip]={ip:v.ip,blocked:0,skipped:0,hidden:0,ago_s:1e9});m.blocked+=v.blocked;m.skipped+=v.skipped;m.hidden+=v.hidden;m.ago_s=Math.min(m.ago_s,v.ago_s);}));y.devices=Object.values(dv);for(const k of ['blocked','skipped','hidden','reports','enforced'])y[k]=ys.reduce((a,x)=>a+(x[k]||0),0);return y;}
function renderBoards(S,D){$('#boards').innerHTML=BOARDS.map((b,i)=>{const s=S[i].status==='fulfilled'?S[i].value:null,d=D[i].status==='fulfilled'?D[i].value:null,addr=b.replace('http://','');const host=s?s.wifi.host.replace('.local',''):addr;
 return `<div class="card"><h3>${esc(host)} <span class="meta">${esc(addr)}${b!==location.origin?` · <a href="${b}/">open</a>`:''}</span></h3>${s?`<div class="row" style="margin:6px 0"><span class="pill on">ONLINE</span>${d?(d.enabled?`<span class="pill on">BLOCKING</span><button class="btn sm" onclick="one('${b}','/api/blocking?on=0')">Turn off</button>`:`<span class="pill warn">OFF · forwarding only</span><button class="btn sm" onclick="one('${b}','/api/blocking?on=1')">Turn on</button>`):''}</div><div class="meta">up ${fmtD(s.uptime_s)} · Wi-Fi ${s.wifi.rssi} dBm${s.ota?' · updates over Wi-Fi':''}</div><div class="meta">${d?`${d.list_count.toLocaleString()} domains · ${d.total.toLocaleString()} queries · ${d.blocked.toLocaleString()} blocked`:'DNS stats unavailable'}</div>`:`<div class="row" style="margin:6px 0"><span class="pill">OFFLINE</span></div><div class="meta">not answering — unplugged, or firmware too old to share its data</div>`}</div>`}).join('');}
function renderDns(d,nowE){
 const pct=d.total?Math.round(d.blocked*100/d.total):0, paused=d.paused_s>0;
 $('#abstate').innerHTML=(paused?`<span class="pill">PAUSED · ${fmtD(d.paused_s)} left</span>`:d.list_count?`<span class="pill on">BLOCKING</span>`:`<span class="pill warn">NO LIST YET · forwarding only</span>`)+(d.off&&d.off.length?` <span class="pill warn">switched off: ${d.off.join(', ')}</span>`:'');
 $('#absub').textContent=`${d.list_count.toLocaleString()} domains · ${d.list_fetched&&nowE?'list updated '+ago(nowE-d.list_fetched):'list not fetched yet'} · ${d.updating?'updating now…':d.update_msg} · DNS servers ${d.listen} · upstream ${d.upstream.join(' / ')}`;
 $('#abstats').innerHTML=`<div class="card"><h3>Queries</h3><div class="big">${d.total.toLocaleString()}</div><div class="meta">since boot, all boards</div></div>
  <div class="card"><h3>Blocked</h3><div class="big bad">${d.blocked.toLocaleString()}</div><div class="meta">${pct}% of queries</div></div>
  <div class="card"><h3>Forwarded</h3><div class="big ok">${d.forwarded.toLocaleString()}</div><div class="meta">${d.timeouts} upstream timeouts</div></div>
  <div class="card"><h3>Devices</h3><div class="big">${d.clients.length}</div><div class="meta">using these DNS servers</div></div>`;
 $('#clients').innerHTML=d.clients.length?d.clients.sort((a,b)=>b.q-a.q).map(c=>`<tr><td>${label(c.ip)}${c.paused_s>0?` <span class="pill warn">paused ${fmtD(c.paused_s)}</span>`:''}</td><td>${c.q}</td><td class="bad">${c.b}</td><td>${c.ext_ago_s>=0&&c.ext_ago_s<1800?'<span class="ok">yes</span>':(c.yt_ago_s>=0&&c.yt_ago_s<900?'<span class="warn">no</span>':'<span class="meta">—</span>')}</td><td>${ago(c.ago_s)}</td><td>${c.paused_s>0?`<button class="btn sm" onclick="act('/api/device?ip=${c.ip}&pause=0')">Resume</button>`:`<button class="btn sm" onclick="act('/api/device?ip=${c.ip}&pause=60')">Pause 1h</button>`}</td></tr>`).join(''):`<tr><td colspan="6">Nothing yet. Set the router's DNS server to ${d.listen}, or set it on one device to try.</td></tr>`;
 $('#recent').innerHTML=d.recent.length?d.recent.map(r=>`<tr><td>${r.t?new Date(r.t*1000).toLocaleTimeString('en-GB'):'—'}</td><td>${NAMES[r.ip]?esc(NAMES[r.ip]):r.ip}</td><td class="dom ${r.blocked?'bad':''}">${r.name}</td><td>${QT[r.type]||r.type}</td><td>${r.blocked?'<span class="pill">blocked</span>':''}</td></tr>`).join(''):'<tr><td colspan="5">—</td></tr>';
}
function renderYt(y,d){
 $('#ytenforce').innerHTML=y.enforce
  ?`<span class="pill">REQUIRED</span><span class="sub" style="width:auto">YouTube in a browser only loads on devices whose extension has checked in · ${y.enforced} lookups held back since boot</span><button class="btn" onclick="act('/api/yt/enforce?on=0')">Make it optional</button>`
  :`<span class="pill on">OPTIONAL</span><span class="sub" style="width:auto">devices without the extension still get YouTube, with ads</span><button class="btn" onclick="act('/api/yt/enforce?on=1')">Require the extension</button>`;
 const seen=d.clients.filter(c=>c.yt_ago_s>=0&&c.yt_ago_s<900), missing=seen.filter(c=>!(c.ext_ago_s>=0&&c.ext_ago_s<1800));
 $('#ytmissing').innerHTML=missing.length?`<span class="pill warn">${missing.length} without the extension</span> opened YouTube in a browser in the last 15 min${y.enforce?' and are being held back until they install it':''}: ${missing.map(c=>`${NAMES[c.ip]?esc(NAMES[c.ip])+' ('+c.ip+')':c.ip} (${ago(c.yt_ago_s)})`).join(', ')} — <a href="/extension">install page</a>`:(seen.length?`every device that opened YouTube in a browser in the last 15 min has the extension`:`no browser has opened YouTube in the last 15 min · <a href="/extension">install page</a>`);
 const who=y.devices.length?y.devices.sort((a,b)=>(b.blocked+b.skipped)-(a.blocked+a.skipped)).map(v=>`${NAMES[v.ip]?esc(NAMES[v.ip]):v.ip} (${v.blocked+v.skipped}, ${ago(v.ago_s)})`).join(' · '):'no browser has reported yet — install the extension from <a href="/extension">this board</a>';
 $('#ytsub').innerHTML=`${y.enabled?'active':'paused along with the ad blocker'} · rules v${y.version} · ${who}`;
 $('#ytstats').innerHTML=`<div class="card"><h3>Ads removed</h3><div class="big bad">${(y.blocked+y.skipped).toLocaleString()}</div><div class="meta">since boot, all browsers</div></div>
  <div class="card"><h3>Stripped before play</h3><div class="big">${y.blocked.toLocaleString()}</div><div class="meta">ad payloads pruned from YouTube's data</div></div>
  <div class="card"><h3>Skipped in player</h3><div class="big">${y.skipped.toLocaleString()}</div><div class="meta">ads that still started, jumped past</div></div>
  <div class="card"><h3>Browsers</h3><div class="big">${y.devices.length}</div><div class="meta">${y.hidden.toLocaleString()} promoted items hidden</div></div>`;
}
function spark(cv,arr){const dpr=devicePixelRatio||1,w=cv.clientWidth,h=cv.clientHeight;cv.width=w*dpr;cv.height=h*dpr;
 const c=cv.getContext('2d');c.scale(dpr,dpr);c.clearRect(0,0,w,h);if(!arr||!arr.length)return;
 const vals=arr.filter(v=>v>=0);const max=Math.max(50,...vals);const n=arr.length,sx=w/Math.max(1,n-1);
 c.lineWidth=1.5;c.strokeStyle='#3b82f6';c.beginPath();let pen=false;
 arr.forEach((v,i)=>{const x=i*sx;if(v<0){pen=false;c.fillStyle='#e74c3c';c.fillRect(x-1,0,2,h);return;}
  const y=h-2-(v/max)*(h-4);if(!pen){c.moveTo(x,y);pen=true}else c.lineTo(x,y)});c.stroke();}
async function tick(){try{
 const s=await getJ(location.origin,'/api/status');
 BOARDS=[location.origin,...(s.peers||[]).map(ip=>'http://'+ip).filter(b=>b!==location.origin)];
 const [S,D,Y,L,hst]=await Promise.all([fromAll('/api/status'),fromAll('/api/dns'),fromAll('/api/yt'),fromAll('/api/lists'),getJ(location.origin,'/api/history')]);
 renderBoards(S,D);
 const dns=D.filter(r=>r.status==='fulfilled').map(r=>r.value), yts=Y.filter(r=>r.status==='fulfilled').map(r=>r.value);
 renderLists(L[0].status==='fulfilled'?L[0].value:{allow:[],block:[],exempt:[],names:[]});
 const dn=mergeDns(dns);renderDns(dn,s.now);renderYt(mergeYt(yts),dn);
 $('#hdr').className=s.state;$('#status').textContent=s.text;
 $('#sub').textContent=`${s.wifi.host} · ${s.wifi.ssid} · ${s.wifi.ip} · gateway ${s.wifi.gw} · Wi-Fi ${s.wifi.rssi} dBm · probing every ${s.interval_ms/1000}s · ${BOARDS.length} board${BOARDS.length>1?'s':''}`;
 let html='';
 s.targets.forEach((t,i)=>{html+=`<div class="card"><h3>${t.name}</h3><div class="big ${cls(t.rtt)}">${t.rtt<0?'✕':t.rtt+'<small> ms</small>'}</div><div class="meta">avg ${t.avg<0?'—':t.avg+' ms'} · loss ${t.loss}% over 15 min · ${t.host}</div><canvas id="c${i}"></canvas></div>`});
 html+=`<div class="card"><h3>DNS</h3><div class="big ${cls(s.dns.rtt)}">${s.dns.rtt<0?'✕':s.dns.rtt+'<small> ms</small>'}</div><div class="meta">resolving one.one.one.one</div><canvas id="cd"></canvas></div>`;
 $('#cards').innerHTML=html;
 hst.rtt.forEach((a,i)=>spark($('#c'+i),a));spark($('#cd'),hst.dns);
 $('#outages').innerHTML=s.outages.length?s.outages.map(o=>`<tr><td>${fmtT(o.start)}</td><td>${o.end?fmtT(o.end):'<span class="pill">ongoing</span>'}</td><td>${o.end?fmtD(o.end-o.start):fmtD(s.now-o.start)}</td><td>${o.kind==='down_lan'?'Router unreachable':'ISP / upstream'}</td></tr>`).join(''):'<tr><td colspan="4" class="ok">No outages recorded since boot</td></tr>';
 $('#foot').textContent=`netmon + ad blocker on ESP32-S3 · up ${fmtD(s.uptime_s)} · booted ${fmtT(s.boot)} (${esc(s.reset_reason||'?')}) · ${s.rounds} probe rounds · ${s.now?'clock synced':'clock not synced yet'}`;
}catch(e){$('#status').textContent='Cannot reach netmon';$('#hdr').className='down_lan'}}
tick();setInterval(tick,5000);
</script></body></html>)rawliteral";

// ---------------- Extension install page (served at /extension) ----------------
static const char INSTALL_HTML[] PROGMEM = R"rawliteral(<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Ad-free YouTube — adblocker</title>
<style>
:root{--bg:#0b0f14;--card:#121821;--line:#1f2933;--fg:#e6edf3;--mut:#8b98a5;--ok:#2ecc71;--info:#3b82f6}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:16px/1.5 -apple-system,system-ui,"Segoe UI",Roboto,sans-serif}
.wrap{max-width:720px;margin:0 auto;padding:28px 20px}h1{font-size:24px;margin:0 0 6px}.sub{color:var(--mut);margin:0 0 22px}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:18px 20px;margin-bottom:14px}
h2{font-size:15px;margin:0 0 10px;color:var(--mut);text-transform:uppercase;letter-spacing:.04em}
ol{margin:0;padding-left:22px}li{margin:6px 0}code{background:#1f2933;padding:2px 6px;border-radius:6px;font-size:14px}
.btn{display:inline-block;background:var(--info);color:#fff;text-decoration:none;font-weight:600;padding:10px 16px;border-radius:8px;margin:4px 0 8px}
a{color:var(--info)}.mut{color:var(--mut)}
</style></head><body><div class="wrap">
<h1>Ad-free YouTube in this browser</h1>
<p class="sub">The board blocks ads everywhere except inside YouTube, which hides them in the video itself. This small extension removes them in the browser. Install once per computer; rules update automatically from the board.</p>
<p class="sub">If YouTube shows “site can’t be reached” on this computer, this network requires the extension: install it, then reload YouTube. It works within a minute.</p>
<div class="card"><h2>1 · Download</h2>
<a class="btn" href="/extension/ytguard.zip">Download ytguard.zip</a>
<div class="mut">Unzip it and keep the <code>ytguard</code> folder somewhere permanent, e.g. Documents. The browser loads it from there every start.</div></div>
<div class="card"><h2>2 · Chrome, Edge, Brave</h2><ol>
<li>Type <code>chrome://extensions</code> in the address bar (Edge: <code>edge://extensions</code>).</li>
<li>Turn on <b>Developer mode</b> (top right).</li>
<li>Click <b>Load unpacked</b> and choose the <code>ytguard</code> folder.</li>
<li>Open YouTube. Done.</li></ol></div>
<div class="card"><h2>2 · Firefox</h2><ol>
<li>Type <code>about:debugging#/runtime/this-firefox</code>, click <b>Load Temporary Add-on</b>, pick <code>manifest.json</code> inside the folder. This lasts until Firefox restarts.</li>
<li>For a permanent install use Firefox ESR or Developer Edition: in <code>about:config</code> set <code>xpinstall.signatures.required</code> to false, rename the zip to <code>ytguard.xpi</code>, then <code>about:addons</code> → gear → <b>Install Add-on From File</b>.</li></ol></div>
<div class="card"><h2>3 · Check</h2>
<p>Within a minute of opening a YouTube page, this computer appears under <b>YouTube ads (browser extension)</b> on the <a href="/">dashboard</a>. Devices that open YouTube without it are listed there too.</p>
<p class="mut">Phones and TVs: this is for browsers only. Use ReVanced or NewPipe on Android and SmartTube on Android TV.</p></div>
</div></body></html>)rawliteral";
