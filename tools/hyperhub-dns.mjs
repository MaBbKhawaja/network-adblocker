// Set the DNS servers the Hyperoptic Hyperhub (Zyxel EX3301-T0) hands out over DHCP, by driving its web UI.
// The UI is a Vue SPA with encrypted API calls, so this talks to a headless Chrome over the DevTools protocol.
//
//   1. start a headless Chrome for Testing with remote debugging, e.g.
//      "Google Chrome for Testing" --headless=new --remote-debugging-port=9333 --user-data-dir=/tmp/hh --ignore-certificate-errors about:blank &
//   2. node tools/hyperhub-dns.mjs 9333 "$ROUTER_USER" "$ROUTER_PASSWORD" 192.168.1.53 ""        # board only (normal)
//      node tools/hyperhub-dns.mjs 9333 "$ROUTER_USER" "$ROUTER_PASSWORD" 1.1.1.1 8.8.8.8      # emergency: board unplugged
//   Devices pick the change up when they renew their lease or reconnect to Wi-Fi.
// Notes: the router rejects DNS 2 == DNS 1 silently; an empty field reads back as "...". Only one admin session at a time.
const [, , port, user, pass, dns1, dns2] = process.argv;
const base = `http://127.0.0.1:${port}`;
let page = (await (await fetch(base + '/json/list')).json()).find(t => t.type === 'page');
if (!page) page = await (await fetch(`${base}/json/new?about:blank`, { method: 'PUT' })).json();
const ws = new WebSocket(page.webSocketDebuggerUrl); await new Promise(r => (ws.onopen = r));
let id = 0; const pending = new Map();
ws.onmessage = e => { const m = JSON.parse(e.data); if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); } };
const send = (method, params = {}) => new Promise(res => { const i = ++id; pending.set(i, res); ws.send(JSON.stringify({ id: i, method, params })); setTimeout(() => res({ timeout: method }), 20000); });
const sleep = ms => new Promise(r => setTimeout(r, ms));
const ev = async expression => (await send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true })).result?.result?.value;
const waitFor = async (expr, ms = 20000) => { const t = Date.now(); while (Date.now() - t < ms) { if (await ev(expr)) return true; await sleep(400); } return false; };
async function typeInto(selector, text) {
  await ev(`(() => { const e = document.querySelector(${JSON.stringify(selector)}); e.focus(); e.value = ''; e.dispatchEvent(new Event('input', {bubbles:true})); return !!e; })()`);
  for (const ch of text) { await send('Input.dispatchKeyEvent', { type: 'keyDown', text: ch, key: ch, unmodifiedText: ch }); await send('Input.dispatchKeyEvent', { type: 'keyUp', key: ch }); }
  await ev(`document.querySelector(${JSON.stringify(selector)}).dispatchEvent(new Event('change', {bubbles:true}))`);
}
const norm = v => (v === '...' ? '' : v);
const READ = `JSON.stringify({dns1:[1,2,3,4].map(i=>document.getElementById('a_ls_dnsaddr1_'+i)?.value).join('.'), dns2:[1,2,3,4].map(i=>document.getElementById('a_ls_dnsaddr2_'+i)?.value).join('.')})`;
const read = async () => { const r = JSON.parse(await ev(READ)); r.dns1 = norm(r.dns1); r.dns2 = norm(r.dns2); return r; };
async function setField(n, ip) {
  const oct = (ip || '').split('.');
  await ev(`(() => { ${[1,2,3,4].map(i => `{ const e = document.getElementById('a_ls_dnsaddr${n}_${i}'); e.focus(); e.value = ${JSON.stringify(oct[i-1] || '')}; e.dispatchEvent(new Event('input', {bubbles:true})); e.dispatchEvent(new Event('change', {bubbles:true})); }`).join('')} document.activeElement && document.activeElement.blur(); return true; })()`);
  await sleep(500);
}
await send('Page.enable'); await send('Runtime.enable');
await send('Page.navigate', { url: 'https://192.168.1.1/login' });
if (!await waitFor(`!!document.querySelector('#username')`)) { console.log('login form never appeared'); process.exit(2); }
await sleep(800); await typeInto('#username', user); await typeInto('input#userpassword[type=password]', pass); await sleep(300);
await ev(`document.querySelector('#loginBtn').click()`);
const loggedIn = await waitFor(`!location.pathname.includes('login') && !!document.querySelector('#app').__vue__`, 15000);
console.log('login:', loggedIn ? 'ok' : 'FAILED'); if (!loggedIn) process.exit(3);
await ev(`document.querySelector('#app').__vue__.$router.push('/HomeNetworking')`);
if (!await waitFor(`!!document.getElementById('a_ls_dnsaddr2_1')`)) { console.log('LAN page never appeared'); process.exit(4); }
await sleep(1500);
console.log('before:', JSON.stringify(await read()));
await setField(1, dns1); await setField(2, dns2);
const typed = await read(); console.log('typed: ', JSON.stringify(typed));
if (typed.dns1 !== dns1 || typed.dns2 !== dns2) { console.log('fields did not take, not applying'); process.exit(5); }
const clicked = await ev(`(() => { const b = [...document.querySelectorAll('button')].find(b => b.textContent.trim() === 'Apply'); if (!b) return false; b.click(); return true; })()`);
console.log('apply clicked:', clicked); await sleep(2500);
console.log('after apply:', await ev(`(() => { const b = [...document.querySelectorAll('button')].find(b => /^(OK|Yes|Confirm)$/i.test(b.textContent.trim()) && b.offsetParent); if (b) { b.click(); return 'confirmed ' + b.textContent.trim(); } return 'no dialog'; })()`));
await sleep(6000);
await ev(`document.querySelector('#app').__vue__.$router.push('/Landing')`); await sleep(2000);
await ev(`document.querySelector('#app').__vue__.$router.push('/HomeNetworking')`);
await waitFor(`!!document.getElementById('a_ls_dnsaddr2_1')`); await sleep(1500);
console.log('readback:', JSON.stringify(await read()));
await ev(`(() => { const lo = [...document.querySelectorAll('a,li,span,button')].find(e => e.textContent.trim() === 'Logout'); lo && lo.click(); })()`); await sleep(1200);
ws.close(); process.exit(0);
