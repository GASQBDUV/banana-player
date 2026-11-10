// Renders promo.html's variants to dist/promo/ with headless Chrome (macOS path; CHROME=... elsewhere).
//   node tools/promo/render.mjs
// The handheld pictures are tools/promo/img/*.png (crops of docs/img/screen-*.png, and a scripted shot of Peaks); the dashboard's are docs/img/web-*.png.
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const out = path.join(here, '../../dist/promo');
fs.mkdirSync(out, { recursive: true });
const CHROME = process.env.CHROME || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const port = 9600 + Math.floor(Math.random() * 300);
const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'bp-promo-'));
const chrome = spawn(CHROME, ['--headless=new', `--remote-debugging-port=${port}`, `--user-data-dir=${profile}`, '--hide-scrollbars',
	'--allow-file-access-from-files', '--force-color-profile=srgb', '--no-first-run', 'about:blank'], { stdio: 'ignore' });
const sleep = ms => new Promise(r => setTimeout(r, ms));

let targets;
for (let i = 0; i < 50; i++) { try { targets = await (await fetch(`http://127.0.0.1:${port}/json`)).json(); if (targets.find(t => t.type === 'page')) break; } catch {} await sleep(200); }
const ws = new WebSocket(targets.find(t => t.type === 'page').webSocketDebuggerUrl);
await new Promise(r => ws.onopen = r);
let id = 0; const pending = new Map();
ws.onmessage = m => { const d = JSON.parse(m.data); if (d.id && pending.has(d.id)) { pending.get(d.id)(d); pending.delete(d.id); } };
const send = (method, params = {}) => new Promise(r => { const i = ++id; pending.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });
await send('Page.enable');

const page = 'file://' + path.join(here, 'promo.html');
for (const [v, w, h, name] of [['post', 1080, 1350, 'ig-post'], ['square', 1080, 1080, 'ig-square'], ['c1', 1080, 1350, 'ig-carousel-1'], ['c2', 1080, 1350, 'ig-carousel-2'], ['c3', 1080, 1350, 'ig-carousel-3']]) {
	await send('Emulation.setDeviceMetricsOverride', { width: w, height: h, deviceScaleFactor: 1, mobile: false });
	await send('Page.navigate', { url: page + '?' + v + '#' + v });
	for (let i = 0; i < 50; i++) { const r = await send('Runtime.evaluate', { expression: 'document.title', returnByValue: true }); if (r.result.result.value === 'ready') break; await sleep(100); }
	await sleep(300);
	const r = await send('Page.captureScreenshot', { format: 'png', clip: { x: 0, y: 0, width: w, height: h, scale: 1 } });
	fs.writeFileSync(path.join(out, name + '.png'), Buffer.from(r.result.data, 'base64'));
	console.log('dist/promo/' + name + '.png');
}
ws.close(); chrome.kill();
await new Promise(r => chrome.once("exit", r));
try { fs.rmSync(profile, { recursive: true, force: true }); } catch {}
