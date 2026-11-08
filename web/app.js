// Banana Player web dashboard. Preact + htm, no build step. Sections: store, api, connection, browser player, uploads,
// small components, pages (Now Playing, Files, Playlists), shell.
import { html, render, useState, useEffect, useLayoutEffect, useRef, useMemo, useCallback } from './vendor/htm-preact-standalone.module.js';
// this tab's own id, kept across its reloads (sessionStorage is per tab): the server swaps this tab's old live stream
// for the new one at once
const TAB_ID = (() => { try { let t = sessionStorage.getItem('bpTab'); if (!t) { t = Math.random().toString(36).slice(2, 10); sessionStorage.setItem('bpTab', t); } return t; } catch (e) { return Math.random().toString(36).slice(2, 10); } })();

/* ================= store ================= */
// iPhone and iPad (every browser there is Safari underneath): playing in the page is off for now. Safari's history
// and audio session broke it in ways not yet pinned down; the handheld plays, the page is the remote
const NO_HERE = /iPad|iPhone|iPod/.test(navigator.userAgent) || (navigator.platform === 'MacIntel' && navigator.maxTouchPoints > 1);
const NO_HERE_WHY = 'Not on iPhone or iPad yet';
const store = {
	// authed: null till the first answer; signedOut: why the sign-in page shows ('pin', 'expired', '')
	authed: null, signedOut: '', epoch: 0,
	// conn: 'ok', 'blip' (failing, the app stays; reconnecting: its banner, after 2 s), 'down' (the down screen)
	conn: 'ok', reconnecting: false, downReason: '', probing: false, nextTry: 0,
	snap: null, snapAt: 0, eq: null,
	target: !NO_HERE && lsGet('bpTarget') === 'browser' ? 'browser' : 'handheld',
	toasts: [], modals: [], lightbox: null, pick: null, sel: new Set(), upload: { items: [], min: false },
	help: false, dropping: false, fsTick: 0, plTick: 0, route: parseRoute(), fresh: {}, media: null,
};
const listeners = new Set();
let version = 0;
function set(patch) { Object.assign(store, patch); version++; listeners.forEach(f => f()); }
function notify() { set({}); }
// subscribes before paint, and catches up with a change made between the render and the subscription (boot can
// answer in that gap: the page stayed on "Connecting…")
function useStore() {
	const [, n] = useState(0), seen = useRef(version);
	seen.current = version;
	useLayoutEffect(() => { const f = () => n(x => x + 1); listeners.add(f); if (version !== seen.current) f(); return () => listeners.delete(f); }, []);
	return store;
}
function lsGet(k) { try { return localStorage.getItem(k); } catch (e) { return null; } }
function lsSet(k, v) { try { localStorage.setItem(k, v); } catch (e) {} }

/* ================= routes: #/playing, #/files/<vpath>, #/playlists[/<id>] ================= */
function parseRoute() {
	const h = location.hash.replace(/^#\/?/, '');
	const [a, ...rest] = h.split('/');
	const tail = rest.map(decodeURIComponent).join('/');
	if (a === 'files') return { page: 'files', path: tail };
	if (a === 'playlists') return { page: 'playlists', id: tail };
	return { page: 'playing' };
}
function go(hash) { location.hash = hash; }
/* which history entries this page made: Back (the player's chevron) only steps into one of them. An entry from an
   earlier load of the page (an update's reload, a tab iOS dropped, the QR's link opened again) would load the page
   anew, and whatever plays here would stop. Each entry carries this load's id and how many of ours lie behind it */
/* Safari also skips, going back, an entry that was left by a step made without a tap (the player opened by itself
   after the QR's link): back from there leaves the page. So each step says whether it came from a tap (bpOk), and
   the one before it too (bpOkPrev, for the sheet over the player) */
const DOC = Math.random().toString(36).slice(2);
let histIdx = 0, curOk = false, replacing = false, uaSwipe = false;
const tapped = () => !navigator.userActivation || navigator.userActivation.isActive;
function stampHist(extra) { try { history.replaceState(Object.assign({}, history.state || {}, { bpOk: curOk }, extra || {}, { bpDoc: DOC, bpIdx: histIdx }), ''); } catch (x) {} }
function pushHist(state, url) { const ok = tapped(), prev = curOk; histIdx++; curOk = ok; history.pushState(Object.assign({}, state || {}, { bpDoc: DOC, bpIdx: histIdx, bpOk: ok, bpOkPrev: prev }), '', url); }
function replaceHist(state, url) { history.replaceState(Object.assign({}, state || {}, { bpDoc: DOC, bpIdx: histIdx, bpOk: curOk }), '', url); }
function replaceHash(hash) { if (new URL(hash, location.href).hash === location.hash) return; replacing = true; location.replace(hash); }	// (the same address: no hashchange to clear the flag)
// n steps back stay on this page (and on entries Safari won't skip)
function canGoBack(n) { const st = history.state; return !!st && st.bpDoc === DOC && st.bpIdx >= n && st.bpOk && (n < 2 || st.bpOkPrev); }
stampHist();
addEventListener('popstate', e => {
	// (iOS's edge swipe animates the change itself: no second animation of ours on top)
	uaSwipe = !!e.hasUAVisualTransition;
	const st = e.state;
	// (no state: a new entry for a changed #, which fires this too; hashchange counts it)
	if (st && st.bpDoc === DOC) { histIdx = st.bpIdx; curOk = !!st.bpOk; } else if (st) { histIdx = 0; curOk = false; stampHist(); }
});
const filesHash = p => '#/files' + (p ? '/' + p.split('/').map(encodeURIComponent).join('/') : '');
addEventListener('hashchange', e => {
	// a new entry (a link, go()) is one more of ours; a replaced one (replaceHash) takes the same place
	if (!(history.state && history.state.bpDoc === DOC)) {
		if (replacing) stampHist();
		else { const ok = tapped(), prev = curOk; histIdx++; curOk = ok; stampHist({ bpOk: ok, bpOkPrev: prev }); }
	}
	replacing = false;
	const swiped = uaSwipe; uaSwipe = false;
	if (/^#pin=/.test(location.hash)) { pinFromHash(); return; }
	// where the player folds back to: the page it was opened from
	const was = store.route, r = parseRoute();
	const apply = () => {
		if (was.page !== 'playing' && r.page === 'playing') { let old = ''; try { old = new URL(e.oldURL).hash; } catch (x) {} set({ backHash: old || '', backNav: !!old, npAnim: Date.now(), sheetOpen: false }); }
		else if (r.page !== 'playing') set({ sheetOpen: false });
		set({ route: r });
	};
	navMotion(swiped ? '' : navKind(was, r), apply);
});
/* motion that tells where you went: deeper slides in from the right (not in Files: folders switch at once), back slides out to the right, a sibling tab
   cross-fades, the player rises from the small one (its cover, title and play button morph). View Transitions where
   the browser has them (Chrome, Safari 18); elsewhere the player's own slide, and the pages simply switch */
const depth = r => r.page === 'files' ? (r.path ? r.path.split('/').length : 0) : r.page === 'playlists' ? (r.id ? 1 : 0) : 0;
function navKind(a, b) {
	const compact = !matchMedia('(min-width: 1024px)').matches, has = view().has;
	// (not over this browser's own video: WebKit froze it for seconds after a View Transition took it along; the
	// player's own slide then)
	if (compact && has && (a.page === 'playing') !== (b.page === 'playing')) return videoHere() ? '' : b.page === 'playing' ? 'player-open' : 'player-close';
	if (a.page !== b.page) return 'fade';
	if (b.page === 'files') return '';	// folder to folder: at once (a slide every step was too much while browsing)
	const d = depth(b) - depth(a);
	return d > 0 ? 'fwd' : d < 0 ? 'back' : (a.path !== b.path || a.id !== b.id) ? 'fade' : '';
}
let vtNow = null;
// a tap while a page changes: the change ends at once and the tap goes through (during a View Transition the browser
// would give the press to the document, not to what's under the finger)
addEventListener('pointerdown', () => { if (vtNow) { try { vtNow.skipTransition(); } catch (x) {} } }, true);
function navMotion(kind, apply) {
	if (!kind || !document.startViewTransition || reduceMotion() || document.hidden || store.authed !== true) { apply(); return; }
	if (vtNow) { try { vtNow.skipTransition(); } catch (x) {} }
	const root = document.documentElement;
	root.dataset.vt = kind;
	store.vtKind = kind;
	try {
		// (rendering is held while the page changes: Preact draws in a microtask, so a timer comes after it; no frames)
		const t = vtNow = document.startViewTransition(() => new Promise(res => { apply(); setTimeout(res, 0); }));
		t.ready.catch(() => {}); t.updateCallbackDone.catch(() => {});
		t.finished.catch(() => {}).then(() => { if (vtNow === t) { delete root.dataset.vt; store.vtKind = ''; vtNow = null; } });
	} catch (x) { delete root.dataset.vt; store.vtKind = ''; apply(); }
}

/* ================= helpers ================= */
const fmtTime = s => { s = Math.max(0, Math.floor(s || 0)); const h = Math.floor(s / 3600), m = Math.floor(s % 3600 / 60), x = s % 60; return (h ? h + ':' + String(m).padStart(2, '0') : m) + ':' + String(x).padStart(2, '0'); };
function fmtSize(b) { if (b == null) return ''; const u = ['B', 'KB', 'MB', 'GB', 'TB']; let i = 0; while (b >= 1000 && i < 4) { b /= 1000; i++; } return (i > 1 ? b.toFixed(1) : Math.round(b)) + ' ' + u[i]; }
function fmtDate(t) { return t ? new Date(t * 1000).toLocaleDateString(undefined, { year: 'numeric', month: 'short', day: 'numeric' }) : ''; }
const baseName = p => p.split('/').pop();
const dirName = p => p.split('/').slice(0, -1).join('/');
const joinPath = (d, n) => d ? d + '/' + n : n;
const stem = n => n.replace(/\.[^.]+$/, '');
const plural = (n, a, b) => n.toLocaleString() + ' ' + (n === 1 ? a : b);
const cmpName = (a, b) => a.localeCompare(b, undefined, { numeric: true, sensitivity: 'base' });
// what a file is called everywhere: its tag title, else its name without the extension and a leading track number
function titleFor(p, tag) {
	const name = baseName(p || '');
	if (tag && tag !== name && tag !== stem(name)) return tag;
	const s = stem(name), t = s.replace(/^\d{1,3}(\s*[-._]\s*|\s+)(?=\S)/, '');
	return t || s;
}
const chapterTitle = (c, i) => c && c.title && !/^\(?unnamed\)?$/i.test(c.title) ? c.title : 'Chapter ' + (i + 1);
const itemsText = (n, more) => more ? (n || 9999).toLocaleString() + '+ items' : plural(n || 0, 'item', 'items');
const EXT = { mp3: 'audio', wav: 'audio', flac: 'audio', ogg: 'audio', opus: 'audio', m4a: 'audio', aac: 'audio', ape: 'audio', wma: 'audio', mp4: 'video', mkv: 'video', webm: 'video', avi: 'video', mov: 'video', m4v: 'video', wmv: 'video', mpg: 'video', jpg: 'photo', jpeg: 'photo', png: 'photo', gif: 'photo', webp: 'photo', bmp: 'photo', srt: 'sub', ass: 'sub', ssa: 'sub', vtt: 'sub', sub: 'sub', idx: 'sub', lrc: 'sub' };
const kindOfName = n => EXT[(n.split('.').pop() || '').toLowerCase()] || 'other';
const isIOS = /iP(hone|ad|od)/.test(navigator.userAgent) || (navigator.platform === 'MacIntel' && navigator.maxTouchPoints > 1);
const touchOnly = matchMedia('(hover: none)').matches;
const throttled = (fn, ms) => { let t = 0, timer = 0, last; return (...a) => { last = a; const wait = ms - (Date.now() - t); clearTimeout(timer); if (wait <= 0) { t = Date.now(); fn(...last); } else timer = setTimeout(() => { t = Date.now(); fn(...last); }, wait); }; };
// a tap or a key, right now: what may start playback (never a reconnect, a timer or a headset coming back)
const userTap = () => !navigator.userActivation || navigator.userActivation.isActive;

/* ================= api ================= */
class ApiError extends Error { constructor(status, body) { super((body && body.error) || 'Request failed (' + status + ')'); this.status = status; this.body = body || {}; } }
function formBody(params) {
	const b = new URLSearchParams();
	for (const [k, v] of Object.entries(params || {})) {
		if (v == null) continue;
		if (Array.isArray(v)) v.forEach(x => b.append(k + '[]', x)); else b.append(k, v);
	}
	return b;
}
// fetch with a deadline: a handheld frozen asleep accepts the connection and never answers
async function fetchT(url, init, ms) {
	const ctl = new AbortController(), t = setTimeout(() => ctl.abort(), ms);
	try { const r = await fetch(url, Object.assign({ credentials: 'same-origin', signal: ctl.signal }, init)); const text = await r.text(); return { r, text }; }
	finally { clearTimeout(t); }
}
// opts: {ms: the deadline, quiet: a failure says nothing about the connection (a slow look into a file)}
async function api(path, params, method, opts) {
	opts = opts || {};
	method = method || (params === undefined || params === null || params._get ? 'GET' : 'POST');
	let url = path; const init = { method };
	if (method === 'GET') { if (params) { const q = formBody(Object.assign({}, params, { _get: undefined })).toString(); if (q) url += '?' + q; } }
	else { init.headers = { 'X-BP': '1', 'Content-Type': 'application/x-www-form-urlencoded' }; init.body = formBody(params).toString(); }
	let r, text;
	try { ({ r, text } = await fetchT(url, init, opts.ms || (method === 'GET' ? 8000 : 10000))); } catch (e) { if (!opts.quiet) connFail(); throw new ApiError(0, { error: 'offline' }); }
	let body = null; try { body = text ? JSON.parse(text) : null; } catch (e) {}
	if (path === '/api/state' && r.ok) seeVersion(r.headers.get('X-BP-Version'));
	connAnswered();
	if (r.status === 401 && path !== '/api/login') { if (store.authed) signOutHere('expired'); throw new ApiError(401, body); }
	if (!r.ok) throw new ApiError(r.status, body);
	return body;
}
const get = (path, params, opts) => api(path, Object.assign({ _get: 1 }, params), 'GET', opts);
const post = (path, params) => api(path, params || {}, 'POST');
const errMsg = e => e.status === 0 ? "Can't reach your handheld" : e.status === 503 ? 'Your handheld is busy. Try again in a moment.' : e.message;
// a failed request's toast; none while the connection banner or screen already says it
const errToast = e => { if (e && e.status === 0 && store.conn !== 'ok') return; toast(errMsg(e), null, 0, e && e.status === 0); };

/* the visual viewport (what the keyboard leaves of the screen): dialogs keep to it; iOS pans the page under the
   keyboard instead of resizing it, which also scrolls a fixed shell out of place, so that pan is undone */
if (window.visualViewport) {
	const vv = visualViewport, root = document.documentElement;
	const fit = () => {
		root.style.setProperty('--vv-h', vv.height + 'px');
		root.style.setProperty('--vv-top', vv.offsetTop + 'px');
		const f = document.activeElement;
		if (f && /^(INPUT|TEXTAREA)$/.test(f.tagName) && f.closest('.modal')) f.scrollIntoView({ block: 'nearest' });
	};
	vv.addEventListener('resize', fit); vv.addEventListener('scroll', fit); fit();
	// the keyboard went: the page back where it was
	addEventListener('focusout', () => setTimeout(() => { if (!document.activeElement || !/^(INPUT|TEXTAREA|SELECT)$/.test(document.activeElement.tagName)) window.scrollTo(0, 0); }, 50));
}

/* ================= toasts, modals ================= */
let toastId = 0;
function toast(text, action, ms, offline) {
	const id = ++toastId;
	set({ toasts: [...store.toasts.filter(t => t.text !== text).slice(-2), { id, text, action, offline }] });
	// it sinks away, then goes
	setTimeout(() => { const t = store.toasts.find(x => x.id === id); if (!t) return; t.out = true; notify(); setTimeout(() => set({ toasts: store.toasts.filter(x => x.id !== id) }), 200); }, ms || (action ? 6000 : 4000));
	return id;
}
let modalId = 0;
// onDismiss: called when the dialog goes without an answer (Esc, the scrim, the connection lost)
function openModal(renderFn, onDismiss) {
	const id = ++modalId;
	// closing: it plays its way out first (the scrim fades, the sheet sinks), input already free
	const close = () => {
		const m = store.modals.find(x => x.id === id); if (!m || m.out) return;
		if (reduceMotion()) { set({ modals: store.modals.filter(x => x.id !== id) }); return; }
		m.out = true; notify();
		setTimeout(() => set({ modals: store.modals.filter(x => x.id !== id) }), 170);
	};
	set({ modals: [...store.modals, { id, render: renderFn, close, dismiss: () => { close(); onDismiss && onDismiss(); } }] });
	return close;
}
function closeOverlays() { store.modals.slice().forEach(m => m.dismiss()); set({ modals: [], lightbox: null, help: false }); }
// which way the last track change went: the cover and title come in from that side
let trackDir = 0, trackDirAt = 0;
const markTrack = d => { trackDir = d; trackDirAt = Date.now(); };
function ask(title, sub, opts) {
	opts = opts || {};
	return new Promise(res => {
		openModal(close => html`<${Modal} title=${title} sub=${sub} onClose=${() => { close(); res(null); }}
			actions=${html`${(opts.buttons || [{ label: 'Cancel', value: null }, { label: opts.ok || 'OK', value: true, cls: 'primary' }]).map(b =>
				html`<button class="btn ${b.cls || ''}" data-focus=${b.focus ? '1' : undefined} onClick=${() => { close(); res(b.value); }}>${b.label}</button>`)}`}>${opts.body}</${Modal}>`, () => res(null));
	});
}

/* the web app's build: a tab left open across an update reloads to the new one, but never in the middle of something */
let webVersion = null;
function seeVersion(v) {
	if (!v) return;	// an older player says nothing
	if (webVersion == null) { webVersion = v; return; }
	if (v === webVersion || store.updated) return;
	const busy = (B.active && (!el.paused || B.parked != null)) || uploading() || store.modals.length || store.lightbox;
	if (!busy) { location.reload(); return; }
	set({ updated: true });
}

/* ================= connection =================
   ok -> blip (a request or the live stream failed: the app stays, "Reconnecting…" after 2 s) -> down (a goodbye from
   the handheld, or 12 s of failing: the down screen). Probes of /api/ping bring it back; a hidden tab runs no timers. */
const C = { since: 0, tries: 0, timer: 0, bannerTimer: 0, graceTimer: 0, boot: null, okAt: 0, lastEvent: 0, wd: 0, reason: '', busy: false };
function connFail() {
	if (store.authed === false || store.conn !== 'ok') return;
	C.since = Date.now(); C.tries = 0; C.reason = '';
	B.wasPlaying = playingHere();	// (the banner pauses it: the down screen still says it was playing)
	closeSSE();
	set({ conn: 'blip' });
	clearTimeout(C.bannerTimer);
	C.bannerTimer = setTimeout(() => { if (store.conn === 'blip') { el.pause(); set({ reconnecting: true }); } }, 2000);
	schedule(400);
}
// a request answered while failing: try the whole way back now
function connAnswered() { if (store.conn === 'blip' && !C.busy) schedule(0); }
function nextDelay() {
	const t = Date.now() - C.since;
	if (C.reason === 'sleep' ? t < 30e3 : C.tries < 3) return 3000;
	return t < 300e3 ? 10000 : 30000;
}
function schedule(ms) {
	clearTimeout(C.timer); C.timer = 0;
	if (document.hidden) return;
	C.timer = setTimeout(probe, ms);
	if (store.conn === 'down') set({ nextTry: Date.now() + ms });
}
async function probe() {
	clearTimeout(C.timer); C.timer = 0;
	if (C.busy || document.hidden || store.conn === 'ok') return;
	C.busy = true; set({ probing: true });
	const started = Date.now();
	let ok = false, boot = null;
	try {
		const { r } = await fetchT('/api/ping', { cache: 'no-store' }, 3000);
		boot = r.headers.get('X-BP-Boot');
		seeVersion(r.headers.get('X-BP-Version'));
		if (r.status === 404) ok = (await fetchT('/api/state', { cache: 'no-store' }, 4000)).r.status < 500;	// a player without /api/ping
		else ok = r.status >= 200 && r.status < 300;
	} catch (e) {}
	if (ok) ok = await reconnect(boot);
	C.busy = false;
	if (ok) return;
	C.tries++;
	if (store.conn === 'blip' && Date.now() - C.since >= 12000 && C.tries >= 3) goDown('none');
	set({ probing: false });
	schedule(Math.max(500, nextDelay() - (Date.now() - started)));
}
async function reconnect(boot) {
	let s;
	try { s = await get('/api/state'); }
	catch (e) {
		if (e.status === 401) { C.busy = false; settle(); signOutHere(store.authed === null ? '' : 'expired'); return true; }
		return false;
	}
	const restarted = !!(boot && C.boot && boot !== C.boot);
	if (boot) C.boot = boot;
	const wasDown = store.conn === 'down', first = store.authed === null;
	settle();
	C.okAt = Date.now();
	if (restarted) dropBrowser();
	set({ authed: true, conn: 'ok', reconnecting: false, downReason: '', probing: false, epoch: store.epoch + 1, plTick: store.plTick + (restarted ? 1 : 0), toasts: store.toasts.filter(t => !t.offline) });
	applySnap(s);
	openSSE();
	resumeUploads();
	if (wasDown || first) loadExtras();
	if (wasDown && !first) toast('Connected again');
	return true;
}
function settle() { clearTimeout(C.timer); clearTimeout(C.bannerTimer); clearTimeout(C.graceTimer); C.timer = 0; }
function goDown(reason) {
	clearTimeout(C.bannerTimer); clearTimeout(C.graceTimer);
	if (store.conn === 'ok') { C.since = Date.now(); C.tries = 0; }
	if (reason !== 'none') { C.since = Date.now(); C.tries = 0; }
	C.reason = reason;
	closeSSE();
	parkHere();
	closeOverlays();
	holdUploads();
	if ('mediaSession' in navigator) { try { navigator.mediaSession.playbackState = 'paused'; navigator.mediaSession.metadata = null; } catch (e) {} }
	// what was said before is past news on this screen ("Continuing on the handheld…" under "asleep")
	set({ conn: 'down', downReason: reason, reconnecting: false, toasts: [] });
	if (!C.busy) schedule(3000);
}
function onVisible() {
	if (document.hidden || store.authed === false) return;
	if (store.conn !== 'ok') probe();
	else if (store.authed) { openSSE(); refreshState(); }
}
document.addEventListener('visibilitychange', () => {
	if (document.hidden) { clearTimeout(C.timer); C.timer = 0; if (!playingHere()) closeSSE(); }
	else onVisible();
});
addEventListener('online', onVisible);
addEventListener('pageshow', e => { if (e.persisted) onVisible(); });

// signed out by the handheld (a new PIN) or by expiry: the sign-in page, the route kept in the address
function signOutHere(why) {
	dropBrowser(true);
	closeSSE(); closeOverlays(); settle();
	set({ authed: false, signedOut: why, snap: null, conn: 'ok', reconnecting: false, downReason: '' });
}

/* ================= live updates ================= */
let es = null;
function applySnap(s) {
	const prev = store.snap;
	set({ snap: s, snapAt: Date.now() });
	// the handheld plays again (paused -> playing): this page pauses its player. Not on a snapshot that merely still
	// says "playing": Continue here starts the browser a moment before the handheld's pause shows in the state
	// (and only once it has been seen paused on this page's file: opening it there, it can look like playing a moment)
	if (B.active && B.meta && s.path === B.meta.p && s.paused && !s.loading) B.handheldQuiet = true;
	if (s.paused === false && prev && prev.paused === true && B.active && B.handheldQuiet && !el.paused && !B.leaving && Date.now() - (B.announcedAt || 0) > 2000) handheldTookOver();
	if (!prev || prev.playlistSerial !== s.playlistSerial) set({ plTick: store.plTick + 1 });
	updateMediaSession();
}
function openSSE() {
	// hidden, the page listens only while it plays: the handheld playing again must pause it (two sounds otherwise)
	if (es || (document.hidden && !playingHere()) || !store.authed || store.conn !== 'ok') return;
	const me = es = new EventSource('/api/events?tab=' + TAB_ID);
	let heard = false;
	C.lastEvent = Date.now();
	const on = (name, fn) => es.addEventListener(name, e => { heard = true; C.sseRetry = 0; C.lastEvent = Date.now(); let d = {}; try { d = JSON.parse(e.data || '{}'); } catch (x) {} fn(d); });
	on('state', d => applySnap(d));
	on('ping', () => {});
	on('fs', d => set({ fsTick: store.fsTick + 1, fsDir: d.dir }));
	on('playlists', () => set({ plTick: store.plTick + 1, plExternal: true }));
	// started on the handheld from another page: this one's file goes, or Play here would bring two sounds back
	// (not one about this page's own Play: the handheld opening the file paused can look like playing for a moment)
	on('handheld-play', () => { if (B.active && Date.now() - (B.announcedAt || 0) > 1500 && (B.handheldQuiet || Date.now() - B.announcedAt > 8000)) handheldTookOver(); });
	on('bye', d => {
		const r = d.reason || 'quit';
		if (r === 'pin') return signOutHere('pin');
		// the lid closed: the handheld freezes moments later. A lid closed with sleep off says "awake" soon after
		if (r === 'sleep') { clearTimeout(C.graceTimer); C.graceTimer = setTimeout(() => goDown('sleep'), 2500); return; }
		goDown(r);
	});
	on('awake', () => clearTimeout(C.graceTimer));
	// the handheld seeked while this page plays its file: here too
	on('handheld-seek', d => { if (B.active && B.meta && B.parked == null && typeof d.t === 'number' && store.snap && store.snap.path === B.meta.p && Math.abs(el.currentTime - d.t) > .5) el.currentTime = d.t; });
	es.onerror = () => {
		if (es !== me || store.conn !== 'ok') return;
		if (heard) return connFail();
		// refused before a word (the handheld has too many tabs open, say): if it still answers, try the stream again
		// later, quietly, instead of a reconnect loop
		closeSSE();
		C.sseRetry = Math.min(10000, (C.sseRetry || 1500) * 2);
		fetchT('/api/ping', { cache: 'no-store' }, 3000).then(({ r }) => {
			if (r.status >= 500) return connFail();
			refreshState();	// meanwhile, the picture as it is
			clearTimeout(C.sseTimer); C.sseTimer = setTimeout(openSSE, C.sseRetry);
		}).catch(connFail);
	};
	// no event for 35 s (the server sends one every 15): the stream is dead without saying so
	clearInterval(C.wd);
	C.wd = setInterval(() => { if (Date.now() - C.lastEvent > 35000) connFail(); }, 5000);
}
function closeSSE() { if (es) { es.close(); es = null; } clearInterval(C.wd); C.wd = 0; }
function playingHere() { return B.active && !el.paused; }
// this browser's video shows in the full player (the media element sits in its slot)
const videoHere = () => store.target === 'browser' && !!B.meta && B.meta.kind === 'video';
async function refreshState() { try { applySnap(await get('/api/state')); } catch (e) {} }

/* ================= browser player ================= */
const el = document.createElement('video');
el.playsInline = true; el.preload = 'auto'; el.controls = false;
document.getElementById('holder').appendChild(el);
// parked: unloaded when the connection went, at that position (the page never plays it again by itself)
const B = { active: false, queue: [], index: -1, source: null, meta: null, loop: 'no', error: false, leaving: false, parked: null, parkedPlaying: false, vol: Number(lsGet('bpVol') || 80) };
el.volume = B.vol / 100;
// playing here with the page hidden (a locked phone): keep listening; paused and hidden: stop (battery)
el.addEventListener('play', () => {
	openSSE();
	// a Play from anywhere (the mini player, Space, the headset) tells the handheld, which pauses: one sound only
	const s = store.snap;
	if (B.meta && (!s || s.paused === false || !s.browserPlaying || s.path !== B.meta.p)) announce();
});
el.addEventListener('pause', () => { if (document.hidden) closeSSE(); });
// where this page is in its file, for the handheld: its own Play resumes from here (not from its stale paused copy)
let posTimer = 0;
function sendPos() {
	if (!B.active || !B.meta || B.leaving || B.parked != null || store.conn !== 'ok' || !el.getAttribute('src')) return;
	post('/api/browser-pos', { p: B.meta.p, t: (el.currentTime || 0).toFixed(1), playing: el.paused ? 0 : 1 }).catch(() => {});
}
for (const ev of ['play', 'pause', 'seeked']) el.addEventListener(ev, () => {
	sendPos();
	clearInterval(posTimer); posTimer = 0;
	if (!el.paused) posTimer = setInterval(() => { if (el.paused || !B.active) { clearInterval(posTimer); posTimer = 0; } else sendPos(); }, 10000);
});
el.addEventListener('emptied', () => { clearInterval(posTimer); posTimer = 0; });
for (const ev of ['play', 'pause', 'loadedmetadata', 'durationchange', 'waiting', 'playing', 'seeked']) el.addEventListener(ev, () => { updateMediaSession(); notify(); });
el.addEventListener('ended', () => { if (store.conn === 'ok') browserNext(true); else notify(); });
el.addEventListener('error', () => { if (!B.active || !el.getAttribute('src')) return; B.error = true; notify(); const m = B.meta; toast("This browser can't play this file", m && { label: 'Play on handheld', fn: () => handoffToHandheld() }, 8000); });
function announce() {
	const p = B.meta.p;
	if (B.announced === p && Date.now() - B.announcedAt < 1500) return;
	B.announced = p; B.announcedAt = Date.now(); B.handheldQuiet = false;
	post('/api/browser-play', { p }).catch(() => {});
}

function probeOK(m) {
	if (m.playable === 'no') return false;
	if (m.playable === 'maybe' && m.mime) { const hint = m.mime + (m.codecs ? '; codecs="' + m.codecs + '"' : ''); return !!el.canPlayType(hint) || !!el.canPlayType(m.mime); }
	return true;
}
async function probe1(p) {
	// not worth a long wait: unknown, the browser just tries the file
	try { return await get('/api/media', { p }, { ms: 4000, quiet: true }); } catch (e) { return null; }
}
const fileUrl = p => '/api/file?p=' + encodeURIComponent(p);
// items: [{p, title?}]; plays the first one the browser can, skipping the rest it can't. auto: the next song after one
// ended (only while connected); everything else comes from a tap
// dir: the way to look for one it can play (Previous looks back); nav: a step along the list (Next, Previous, a song
// that ended), whose caller says when there's nothing more
async function browserPlay(items, index, source, pos, auto, dir, nav) {
	if (store.conn !== 'ok') return false;
	if (NO_HERE) { toast(NO_HERE_WHY + ': it plays on the handheld'); return false; }
	dir = dir || 1;
	let skipped = 0;
	for (let i = index; i >= 0 && i < items.length; i += dir) {
		const it = items[i], m = await probe1(it.p);
		if (m && !probeOK(m)) { skipped++; continue; }
		if (auto && store.conn !== 'ok') return false;
		const kind = kindOfName(it.p) === 'video' || (m && m.vcodec) ? 'video' : 'audio';
		Object.assign(B, { shuffled: items === B.queue && B.shuffled, active: true, queue: items, index: i, source, error: false, leaving: false, parked: null, parkedPlaying: false,
			meta: { p: it.p, title: titleFor(it.p, m && m.title), artist: m && m.artist || '', album: m && m.album || '', kind, chapters: m && m.chapters || [], duration: m && m.duration || 0, media: m } });
		el.src = fileUrl(it.p);
		subsForFile(m);
		if (pos > 0) el.addEventListener('loadedmetadata', () => { el.currentTime = pos; }, { once: true });
		announce();
		el.play().catch(() => {});
		if (skipped) toast('Skipped ' + plural(skipped, 'file', 'files') + " this browser can't play");
		// no word on it in time: playing anyway; its tags, chapters and tracks when they come
		if (!m) probe1(it.p).then(m2 => { if (!m2 || !B.meta || B.meta.p !== it.p) return; Object.assign(B.meta, { title: titleFor(it.p, m2.title), artist: m2.artist || '', album: m2.album || '', chapters: m2.chapters || [], duration: m2.duration || 0, media: m2 }); subsForFile(m2); notify(); });
		updateMediaSession(); notify();
		return true;
	}
	if (skipped && !nav) toast('Plays on the handheld only', null);
	return false;
}
async function browserNext(auto, dir) {
	dir = dir || 1;
	const n = B.queue.length; let i = B.index + dir;
	if (auto && B.loop === 'one') { el.currentTime = 0; el.play(); return; }
	if (i >= n)
	{
		if (B.loop === 'all') i = 0;
		// a song that ended: the list is over. Next pressed on the last one: it says so and the song plays on (as the
		// handheld's Next does)
		else if (auto) { el.pause(); toast('End of ' + (B.source && B.source.title || 'the list')); return; }
		else { toast('This is the last one'); return; }
	}
	if (i < 0) i = 0;
	if (dir < 0 && el.currentTime > 3) { el.currentTime = 0; return; }
	// (the ones it can't play are passed over; none left that way: the end, as on the last one)
	const ok = await browserPlay(B.queue, i, B.source, 0, auto, dir, true);
	if (ok || store.conn !== 'ok') return;
	if (auto) { el.pause(); toast('End of ' + (B.source && B.source.title || 'the list')); }
	else toast(dir < 0 ? 'This is the first one' : 'This is the last one');
}
function unload() { el.pause(); el.removeAttribute('src'); clearSubs(); el.load(); }
// play pressed on the handheld while this page played: the sound is the handheld's now, and so is the switch (else it
// still says "In this browser" over a page that's silent)
function handheldTookOver() {
	browserStop();
	if (store.target !== 'handheld') { chooseTarget('handheld'); toast('Playing on your handheld now'); }
}
function browserStop() {
	B.leaving = true; unload();
	Object.assign(B, { active: false, meta: null, queue: [], index: -1, error: false, parked: null, parkedPlaying: false });
	post('/api/browser-play', { stop: 1 }).catch(() => {});
	notify();
}
// forgets this page's file without telling the handheld (signed out, or the player started again)
function dropBrowser() {
	B.leaving = true; unload();
	Object.assign(B, { active: false, meta: null, queue: [], index: -1, error: false, parked: null, parkedPlaying: false });
	notify();
}
// the connection went: stop at once (the stream can't go on), keep what and where
function parkHere() {
	if (!B.meta || B.parked != null) return;
	const playing = !el.paused || (store.conn === 'blip' && B.wasPlaying);
	B.parked = bPos(); B.parkedPlaying = playing;
	el.pause(); el.removeAttribute('src'); el.load();
	notify();
}
function playParked() {
	if (!B.meta || B.parked == null) return;
	const pos = B.parked;
	Object.assign(B, { parked: null, parkedPlaying: false, error: false });
	el.src = fileUrl(B.meta.p);
	restoreSubs();
	if (pos > 0) el.addEventListener('loadedmetadata', () => { el.currentTime = pos; }, { once: true });
	el.play().catch(() => {});
	notify();
}
function setBrowserVol(v) { B.vol = v; el.volume = v / 100; lsSet('bpVol', v); notify(); }
function bPos() { return B.parked != null ? B.parked : el.currentTime || 0; }
function bDur() { return isFinite(el.duration) && el.duration ? el.duration : (B.meta && B.meta.duration) || 0; }

/* ---- subtitles in this browser: one <track> per text track, fetched as WebVTT when chosen ---- */
const SUBS = { file: '', list: [], chosen: -1, urls: {}, progress: null, failed: {}, seq: 0 };
function clearSubs() {
	[...el.querySelectorAll('track')].forEach(t => t.remove());
	Object.values(SUBS.urls).forEach(u => URL.revokeObjectURL(u));
	Object.assign(SUBS, { file: '', list: [], chosen: -1, urls: {}, progress: null, failed: {}, seq: SUBS.seq + 1 });
}
function subsForFile(m) {
	clearSubs();
	SUBS.file = B.meta ? B.meta.p : '';
	SUBS.list = m && m.subs || [];
	const want = lsGet('bpSubOff') === '1' ? -1 : m && m.subDefault != null ? m.subDefault : -1;
	const sub = SUBS.list.find(x => x.i === want);
	if (sub && sub.kind !== 'image') chooseSub(want);
}
function restoreSubs() { const c = SUBS.chosen; if (c >= 0) { SUBS.chosen = -1; chooseSub(c); } }
async function chooseSub(i) {
	const file = SUBS.file, seq = SUBS.seq;
	SUBS.chosen = i; SUBS.progress = null; delete SUBS.failed[i];
	[...el.textTracks].forEach(t => t.mode = 'disabled');
	notify();
	if (i < 0) return;
	if (!SUBS.urls[i]) {
		for (;;) {
			let r, text;
			try { ({ r, text } = await fetchT('/api/subs?' + new URLSearchParams({ p: file, i }), {}, 60000)); } catch (e) { r = null; }
			if (SUBS.seq !== seq || SUBS.chosen !== i) return;
			if (r && r.status === 202) {
				let pr = 0; try { pr = JSON.parse(text).progress || 0; } catch (e) {}
				SUBS.progress = pr; notify();
				await new Promise(res => setTimeout(res, 1000));
				if (SUBS.seq !== seq || SUBS.chosen !== i) return;
				continue;
			}
			SUBS.progress = null;
			if (!r || !r.ok) { SUBS.failed[i] = r && r.status === 415 ? "These subtitles can't be shown here" : "Couldn't load these subtitles"; SUBS.chosen = -1; notify(); return; }
			SUBS.urls[i] = URL.createObjectURL(new Blob([text], { type: 'text/vtt' }));
			const t = document.createElement('track');
			t.kind = 'subtitles'; t.src = SUBS.urls[i]; t.dataset.i = i;
			const s = SUBS.list.find(x => x.i === i); t.label = s ? s.label : 'Subtitles'; if (s && s.lang) t.srclang = s.lang;
			t.addEventListener('load', () => placeCues());
			el.appendChild(t);
			break;
		}
	}
	const tr = el.querySelector('track[data-i="' + i + '"]');
	if (tr) { tr.track.mode = 'showing'; placeCues(); }
	notify();
}
// the viewer's own look: size (a share of the picture's height) and how high the lines sit
const CUE_SIZES = [['s', 'Small', .8], ['m', 'Medium', 1], ['l', 'Large', 1.25], ['xl', 'Extra large', 1.5]];
const cueStyle = { size: lsGet('bpCueSize') || 'm', pos: lsGet('bpCuePos') || 'bottom' };
function placeCues() {
	for (const t of el.textTracks) {
		if (t.mode !== 'showing' || !t.cues) continue;
		for (const c of t.cues) { c.snapToLines = true; c.line = cueStyle.pos === 'raised' ? -4 : -2; }
	}
}
function sizeCues() {
	const f = (CUE_SIZES.find(x => x[0] === cueStyle.size) || CUE_SIZES[1])[2];
	el.style.setProperty('--cue', Math.max(12, el.clientHeight * .045 * f).toFixed(1) + 'px');
}
if ('ResizeObserver' in window) new ResizeObserver(sizeCues).observe(el);
function setCueStyle(k, v) { cueStyle[k] = v; lsSet(k === 'size' ? 'bpCueSize' : 'bpCuePos', v); sizeCues(); placeCues(); notify(); }

function updateMediaSession() {
	if (!('mediaSession' in navigator)) return;
	const v = view();
	if (!v.has || store.conn === 'down') { try { navigator.mediaSession.metadata = null; navigator.mediaSession.playbackState = 'paused'; } catch (e) {} return; }
	try {
		navigator.mediaSession.metadata = new MediaMetadata({ title: v.title, artist: v.artist, album: v.album, artwork: v.cover ? [{ src: '/api/cover?p=' + encodeURIComponent(v.cover), sizes: '512x512' }] : [] });
		navigator.mediaSession.playbackState = v.paused ? 'paused' : 'playing';
		// a headset or a car coming back can press Play with nobody there: not while the connection is out, nor just after
		const live = () => store.conn === 'ok' && Date.now() - C.okAt > 3000;
		const h = (a, f) => { try { navigator.mediaSession.setActionHandler(a, f); } catch (e) {} };
		// the page's own file: its own element, never the handheld (a system Pause, or a Play after it, must not
		// start the handheld because the page's view moved on); the handheld's only when this page plays nothing
		const mine = () => B.meta && (B.active || B.parked != null);
		h('play', () => { if (!live()) return; if (mine()) { if (B.parked != null) playParked(); else if (el.paused) el.play().catch(() => {}); } else if (view().paused && store.target === 'handheld') ctl.toggle(); });
		h('pause', () => { if (mine()) el.pause(); else if (!view().paused && store.target === 'handheld') ctl.toggle(); });
		h('previoustrack', () => { if (live()) ctl.prev(); }); h('nexttrack', () => { if (live()) ctl.next(); });
		h('seekbackward', () => ctl.seekRel(-5)); h('seekforward', () => ctl.seekRel(10));
		h('seekto', d => ctl.seek(d.seekTime));
	} catch (e) {}
}

/* the normalised "now playing" view, for whichever target is active */
function view() {
	const s = store.snap;
	if (store.target === 'browser' && B.meta) {
		const m = B.meta;
		return { has: true, here: true, title: m.title, artist: m.artist, album: m.album, kind: m.kind, paused: el.paused, parked: B.parked, loading: false, pos: bPos, duration: bDur(), chapters: m.chapters, chapter: chapterAt(m.chapters, bPos()),
			source: B.source && Object.assign({}, B.source, { index: B.index, count: B.queue.length }), loop: B.loop, shuffled: !!B.shuffled, path: m.p, cover: m.kind === 'audio' ? m.p : '' };
	}
	if (s && s.path) {
		const pos = () => s.paused ? s.position : s.position + (Date.now() - store.snapAt) / 1000 * (s.speed || 1);
		return { has: true, here: false, title: titleFor(s.path, s.title), artist: s.artist, album: s.album, kind: s.kind, paused: s.paused, loading: s.loading, pos, duration: s.duration, chapters: s.chapters || [], chapter: s.chapter, source: s.source, loop: s.loop, shuffled: !!s.shuffled, path: s.path, cover: s.kind === 'audio' ? s.path : '', tracks: s.tracks };
	}
	return { has: false, here: store.target === 'browser', pos: () => 0, duration: 0, chapters: [], chapter: -1, loop: 'no' };
}
function chapterAt(ch, t) { let c = -1; (ch || []).forEach((x, i) => { if (x.start <= t + 0.2) c = i; }); return c; }

/* handheld commands: fire and forget, the SSE brings the new state */
const cmd = (c, extra) => post('/api/cmd', Object.assign({ c }, extra)).catch(errToast);
const hereNow = () => store.target === 'browser' && B.meta;
const ctl = {
	toggle() { if (hereNow()) { if (B.parked != null) playParked(); else el.paused ? el.play().catch(() => {}) : el.pause(); } else cmd('toggle'); },
	next() { markTrack(1); hereNow() ? (B.parked == null && browserNext(false, 1)) : cmd('next'); },
	prev() { markTrack(-1); hereNow() ? (B.parked == null && browserNext(false, -1)) : cmd('prev'); },
	seek(t) { if (hereNow()) { if (B.parked != null) { B.parked = t; notify(); } else el.currentTime = t; } else { cmd('seek', { t: t.toFixed(1) }); if (store.snap) { store.snap.position = t; store.snapAt = Date.now(); notify(); } } },
	seekRel(d) { if (hereNow()) ctl.seek(Math.max(0, Math.min(bDur(), bPos() + d))); else cmd('seekrel', { d }); },
	chapterTo(i) { if (hereNow()) ctl.seek(B.meta.chapters[i].start); else cmd('chapterto', { i }); },
	chapterStep(dir) { const v = view(); if (!v.chapters.length) return; const i = Math.max(0, Math.min(v.chapters.length - 1, v.chapter + dir)); ctl.chapterTo(i); },
	loop() {
		const order = ['no', 'all', 'one'];
		if (hereNow()) { B.loop = order[(order.indexOf(B.loop) + 1) % 3]; notify(); }
		else if (store.snap) cmd('loop', { v: order[(order.indexOf(store.snap.loop) + 1) % 3] });
	},
	// on: the list in a new order, from the one playing; off: its own order again, the one playing kept where it is
	shuffle() {
		if (hereNow()) {
			if (!B.shuffled) {
				B.order = B.queue;
				const cur = B.queue[B.index], rest = B.queue.filter((_, i) => i !== B.index);
				for (let i = rest.length - 1; i > 0; i--) { const j = Math.floor(Math.random() * (i + 1)); [rest[i], rest[j]] = [rest[j], rest[i]]; }
				B.queue = [cur, ...rest]; B.index = 0; B.shuffled = true; notify(); toast('Shuffled');
			} else {
				const p = B.meta.p; B.queue = B.order || B.queue; B.index = Math.max(0, B.queue.findIndex(q => q.p === p)); B.shuffled = false; notify();
			}
			return;
		}
		const sn = store.snap; if (!sn || !sn.path) return;
		if (!sn.shuffled) {
			post('/api/cmd', { c: 'shuffle' }).then(() => toast('Shuffled')).catch(errToast);
			return;
		}
		unshuffleHandheld(sn);
	},
};

// the handheld has no "unshuffle": a playlist is played again in its order, from the one playing, where it is
async function unshuffleHandheld(sn) {
	const t = (sn.paused ? sn.position : sn.position + (Date.now() - store.snapAt) / 1000).toFixed(1), src = sn.source || {};
	try {
		if (src.kind === 'playlist' && src.id) {
			const pl = await get('/api/playlist', { id: src.id }), i = (pl.items || []).findIndex(x => x.p === sn.path);
			if (i >= 0) { await post('/api/play', { playlist: src.id, index: i, t }); return; }
		}
		// (a folder's list: the handheld can't put it back in order without starting the song again)
		toast('Shuffle stays on till this list plays again from Files');
	} catch (e) { errToast(e); }
}

/* ---- target switch and handoff ---- */
function chooseTarget(t) { if (NO_HERE && t === 'browser') { toast(NO_HERE_WHY + ': it plays on the handheld'); return; } lsSet('bpTarget', t); set({ target: t }); }
async function setTarget(t) {
	const s = store.snap;
	// already here, with nothing of its own: the handheld's file, as Continue here
	if (t === store.target) { if (t === 'browser' && !B.meta && s && s.path && s.kind !== 'photo') await continueHere(); return; }
	if (t === 'browser') {
		if (!userTap()) return;
		chooseTarget(t);
		// the handheld's file, unless this page already has that one, paused where the handheld is
		if (s && s.path && s.kind !== 'photo' && (!B.meta || B.meta.p !== s.path || !s.paused)) await continueHere();
	} else {
		if (B.active && B.meta) await handoffToHandheld();
		else chooseTarget(t);
	}
}
async function continueHere() {
	const s = store.snap; if (!s || !s.path || !userTap()) return;
	const pos = s.paused ? s.position : s.position + (Date.now() - store.snapAt) / 1000 * (s.speed || 1);
	const src = s.source ? { kind: s.source.kind, title: s.source.title, id: s.source.id } : null;
	// the file at once; the rest of its list when the handheld has said it (it can take a moment while it switches)
	if (!await browserPlay([{ p: s.path }], 0, src, pos)) { chooseTarget('handheld'); return; }
	chooseTarget('browser'); toast('Continuing here from ' + fmtTime(pos));
	get('/api/source').then(q => {
		const i = q.items ? q.items.findIndex(x => x.p === s.path) : -1;
		if (i >= 0 && B.meta && B.meta.p === s.path && B.queue.length === 1) { B.queue = q.items; B.index = i; notify(); }
	}).catch(() => {});
}
// aid: the audio track to play there (the browser can only play the first)
async function handoffToHandheld(aid) {
	const m = B.meta; if (!m) { chooseTarget('handheld'); return; }
	const pos = bPos(), src = B.source, idx = B.index, was = !el.paused;
	sendPos();
	el.pause();	// one sound: quiet here before the handheld starts
	try {
		if (src && src.kind === 'playlist' && src.id && aid == null) await post('/api/play', { playlist: src.id, index: idx, t: pos.toFixed(1) });
		else await post('/api/play', { p: m.p, t: pos.toFixed(1), aid });
	} catch (e) { if (was && store.conn === 'ok') el.play().catch(() => {}); toast(e.status === 0 ? "Couldn't start it on your handheld" : errMsg(e)); return false; }
	browserStop();
	chooseTarget('handheld');
	toast('Continuing on the handheld from ' + fmtTime(pos));
	return true;
}

// collects playable [{p}] from selected paths (a folder gives its own media, in order)
async function collectMedia(paths) {
	const out = [];
	for (const p of paths) {
		const isFolder = store.folderSet && store.folderSet.has(p);
		if (isFolder) { try { const r = await get('/api/ls', { p }); r.items.filter(i => !i.dir && (i.kind === 'audio' || i.kind === 'video')).sort((a, b) => cmpName(a.name, b.name)).forEach(i => out.push({ p: joinPath(p, i.name) })); } catch (e) {} }
		else if (['audio', 'video'].includes(kindOfName(p))) out.push({ p });
	}
	return out;
}
// "Play" for files/folders, on a target; the target changes only once it plays there
// stay: a row's tap plays and the page stays (the small player shows it); true when it plays
async function playPaths(paths, folderSet, target, stay) {
	if (!userTap()) return;
	store.folderSet = folderSet;
	target = target || store.target;
	if (target === 'browser') {
		let items, src, idx = 0;
		if (paths.length === 1 && !folderSet.has(paths[0])) {
			const d = dirName(paths[0]);
			try { const r = await get('/api/ls', { p: d }); items = r.items.filter(i => !i.dir && (i.kind === 'audio' || i.kind === 'video')).sort((a, b) => cmpName(a.name, b.name)).map(i => ({ p: joinPath(d, i.name) })); } catch (e) { items = [{ p: paths[0] }]; }
			src = { kind: 'folder', title: baseName(d) || 'This card' };
			idx = Math.max(0, items.findIndex(i => i.p === paths[0]));
		} else {
			items = await collectMedia(paths);
			if (!items.length) { toast('Nothing to play there'); return; }
			src = { kind: 'playlist', title: paths.length === 1 ? baseName(paths[0]) : 'Selection' };
		}
		if (!await browserPlay(items, idx, src, 0)) return false;
		chooseTarget('browser');
		if (!stay) go('#/playing');
		return true;
	} else {
		const was = B.active && !el.paused; el.pause();
		try { await post('/api/play', paths.length === 1 ? { p: paths[0] } : { p: paths }); }
		catch (e) { if (was && store.conn === 'ok') el.play().catch(() => {}); toast(e.status === 0 ? "Couldn't start it on your handheld" : errMsg(e)); return false; }
		if (B.active) browserStop();
		chooseTarget('handheld');
		if (!stay) go('#/playing');
		return true;
	}
}
async function playPlaylist(id, name, items, index, shuffle, target, tapped, stay) {
	if (!tapped && !userTap()) return;
	target = target || store.target;
	if (target === 'browser') {
		let list = items.filter(i => !i.missing && i.p).map(i => ({ p: i.p, title: i.title }));
		if (shuffle) list = list.sort(() => Math.random() - .5);
		const idx = shuffle ? 0 : Math.max(0, list.findIndex(i => i.p === items[index || 0].p));
		if (await browserPlay(list, idx, { kind: 'playlist', title: name, id }, 0)) { chooseTarget('browser'); if (!stay) go('#/playing'); return true; }
		return false;
	} else {
		const was = B.active && !el.paused; el.pause();
		try { await post('/api/play', { playlist: id, index: index || 0, shuffle: shuffle ? 1 : 0 }); }
		catch (e) { if (was && store.conn === 'ok') el.play().catch(() => {}); toast(e.status === 0 ? "Couldn't start it on your handheld" : errMsg(e)); return false; }
		if (B.active) browserStop();
		chooseTarget('handheld');
		if (!stay) go('#/playing');
		return true;
	}
}
// a row's tap: it plays at once where the last choice was; one this browser can't play goes to the handheld (said so)
async function playHere(play, p, playable) {
	if (store.target === 'browser') {
		const m = playable === 'no' ? { playable: 'no' } : await probe1(p);
		if (m && !probeOK(m)) { toast('Not supported in this browser · play it on the handheld instead', { label: 'Play on handheld', fn: () => play('handheld') }, 8000); return; }
	}
	await play(store.target);
}

/* ================= uploads ================= */
const UP_MAX = 2, SPACE_MARGIN = 64 * 1024 * 1024;
const uploadable = n => !n.startsWith('.') && (kindOfName(n) !== 'other');
let upSeq = 0;

async function entriesFromDrop(dt) {
	// webkitGetAsEntry must be called synchronously, before the first await
	const entries = [...(dt.items || [])].map(i => i.kind === 'file' && i.webkitGetAsEntry ? i.webkitGetAsEntry() : null).filter(Boolean);
	const plain = [...(dt.files || [])];
	const out = [];
	if (!entries.length) { plain.forEach(f => out.push({ file: f, rel: '' })); return out; }
	for (const e of entries) await walkEntry(e, '', out);
	return out;
}
async function walkEntry(entry, rel, out) {
	if (entry.isFile) { const f = await new Promise(r => entry.file(r, () => r(null))); if (f) out.push({ file: f, rel }); return; }
	if (!entry.isDirectory) return;
	const sub = joinPath(rel, entry.name), reader = entry.createReader();
	for (;;) {
		const batch = await new Promise(r => reader.readEntries(r, () => r([])));
		if (!batch.length) break;
		for (const c of batch) await walkEntry(c, sub, out);
	}
}
function entriesFromInput(files) {
	return [...files].map(f => ({ file: f, rel: f.webkitRelativePath ? dirName(f.webkitRelativePath) : '' }));
}
function dirLabel(dir) { const n = baseName(dir); return n && dir.includes('/') ? n : 'This card'; }

async function startUpload(entries, dir) {
	if (!entries.length) return;
	const ok = entries.filter(e => uploadable(e.file.name));
	if (ok.length < entries.length) toast('Skipped ' + plural(entries.length - ok.length, 'file', 'files') + ' that ' + (entries.length - ok.length === 1 ? "isn't" : "aren't") + ' music, video, a photo or subtitles');
	if (!ok.length) return;
	let list = ok, free = null;
	try { free = (await get('/api/ls', { p: dir })).free; } catch (e) { if (e.status !== 404) return toast(errMsg(e)); }
	const total = list.reduce((a, e) => a + e.file.size, 0);
	if (free != null && total + SPACE_MARGIN > free) {
		const fit = []; let used = SPACE_MARGIN;
		for (const e of list) if (used + e.file.size <= free) { fit.push(e); used += e.file.size; }
		const r = await ask('Not enough space on ' + dirLabel(dir), `These files need ${fmtSize(total)}; there's ${fmtSize(free)} free.`, fit.length ? { buttons: [{ label: 'Cancel', value: null }, { label: 'Upload what fits', value: true, cls: 'primary' }] } : { buttons: [{ label: 'OK', value: null, cls: 'primary' }] });
		if (!r || !fit.length) return;
		list = fit;
	}
	// clashes: ask once per batch, from the target folders' listings
	const names = new Map();
	for (const rel of new Set(list.map(e => e.rel))) {
		try { names.set(rel, new Set((await get('/api/ls', { p: joinPath(dir, rel) })).items.map(i => i.name))); } catch (e) { names.set(rel, new Set()); }
	}
	const clashes = list.filter(e => names.get(e.rel).has(e.file.name));
	let policy = 'skip';
	if (clashes.length) {
		const n = clashes.length, shown = clashes.slice(0, 5).map(e => joinPath(e.rel, e.file.name));
		policy = await ask(n === 1 ? 'A file with this name is already here' : plural(n, 'file', 'files') + ' with these names are already here', null,
			{ body: html`<ul class="names">${shown.map(x => html`<li class="ell">${x}</li>`)}${n > 5 && html`<li class="dim">and ${(n - 5).toLocaleString()} more</li>`}</ul>`,
				buttons: [{ label: 'Skip', value: 'skip' }, { label: 'Replace', value: 'replace', cls: 'danger' }, { label: 'Keep both', value: 'both', cls: 'primary', focus: true }] });
		if (!policy) return;
	}
	const items = [];
	for (const e of list) {
		let name = e.file.name, overwrite = false;
		if (names.get(e.rel).has(name)) {
			if (policy === 'skip') continue;
			if (policy === 'replace') overwrite = true;
			else { let n = 2; const ext = name.includes('.') ? '.' + name.split('.').pop() : '', base = stem(name); while (names.get(e.rel).has(`${base} (${n})${ext}`)) n++; name = `${base} (${n})${ext}`; names.get(e.rel).add(name); }
		}
		items.push({ id: ++upSeq, file: e.file, name, rel: e.rel, dir, size: e.file.size, loaded: 0, state: 'queued', overwrite, error: '', xhr: null });
	}
	if (!items.length) return;
	set({ upload: { items: [...store.upload.items.filter(i => i.state !== 'done' && i.state !== 'cancelled'), ...items], min: false } });
	pumpUploads();
}
function pumpUploads() {
	if (store.conn !== 'ok') return;
	let running = store.upload.items.filter(i => i.state === 'up').length;
	for (const it of store.upload.items) {
		if (running >= UP_MAX) break;
		if (it.state === 'queued') { running++; sendUpload(it); }
	}
}
let lastNote = 0;
function sendUpload(it) {
	const x = new XMLHttpRequest(); it.xhr = x; it.state = 'up'; it.error = '';
	const q = new URLSearchParams({ dir: it.dir, name: it.name }); if (it.rel) q.set('rel', it.rel); if (it.overwrite) q.set('overwrite', '1');
	x.open('PUT', '/api/upload?' + q);
	x.setRequestHeader('X-BP', '1');
	x.upload.onprogress = e => { it.loaded = e.loaded; if (Date.now() - lastNote > 150) { lastNote = Date.now(); notify(); } };
	x.onload = () => {
		it.xhr = null;
		if (x.status >= 200 && x.status < 300) { it.state = 'done'; it.loaded = it.size; markFresh(joinPath(joinPath(it.dir, it.rel), it.name)); set({ fsTick: store.fsTick + 1, fsDir: it.dir }); collapseSoon(); }
		else { it.state = 'failed'; let b = {}; try { b = JSON.parse(x.responseText); } catch (e) {} it.error = x.status === 409 ? 'Already there' : b.error || 'Failed (' + x.status + ')'; if (x.status === 401) signOutHere('expired'); }
		notify(); pumpUploads();
	};
	// the connection, not the file: it waits, and starts again from the beginning once the handheld is back
	x.onerror = () => { it.xhr = null; it.state = 'waiting'; it.loaded = 0; connFail(); notify(); };
	x.onabort = () => { it.xhr = null; };
	x.send(it.file);
	notify();
}
function cancelUpload(it) { if (it.xhr) it.xhr.abort(); it.state = 'cancelled'; set({ upload: { ...store.upload, items: store.upload.items.filter(i => i !== it) } }); pumpUploads(); }
function cancelAllUploads() { store.upload.items.forEach(i => { if (i.state === 'up' || i.state === 'queued' || i.state === 'waiting') { if (i.xhr) i.xhr.abort(); i.state = 'cancelled'; } }); set({ upload: { items: store.upload.items.filter(i => i.state === 'done' || i.state === 'failed'), min: false } }); }
function retryUpload(it) { it.state = 'queued'; it.loaded = 0; notify(); pumpUploads(); }
function holdUploads() { store.upload.items.forEach(i => { if (i.state === 'up' || i.state === 'queued') { const x = i.xhr; i.xhr = null; i.state = 'waiting'; i.loaded = 0; if (x) x.abort(); } }); notify(); }
function resumeUploads() { let any = false; store.upload.items.forEach(i => { if (i.state === 'waiting') { i.state = 'queued'; any = true; } }); if (any) { set({ upload: { ...store.upload, min: false } }); pumpUploads(); } }
// uploads all done: the panel folds to a strip after 4 s
let collapseTimer = 0;
function collapseSoon() {
	clearTimeout(collapseTimer);
	collapseTimer = setTimeout(() => { const it = store.upload.items; if (it.length && it.every(i => i.state === 'done')) set({ upload: { ...store.upload, min: true } }); }, 4000);
}
// rows that just arrived glow for 2 s
function markFresh(p) { set({ fresh: Object.assign({}, store.fresh, { [p]: Date.now() }) }); setTimeout(() => { const f = Object.assign({}, store.fresh); delete f[p]; set({ fresh: f }); }, 2200); }
const uploading = () => store.upload.items.some(i => i.state === 'up' || i.state === 'queued' || i.state === 'waiting');
addEventListener('beforeunload', e => { if (uploading()) { e.preventDefault(); e.returnValue = ''; } });

/* ================= small components ================= */
const dimsCache = {};
function Icon({ n, k, cls, label }) {
	let d = dimsCache[n];
	if (!d) { const s = document.getElementById('g-' + n); const vb = s && s.viewBox.baseVal; d = dimsCache[n] = vb ? [vb.width, vb.height] : [9, 9]; }
	k = k || 2;
	return html`<svg class="ic ${cls || ''}" width=${d[0] * k} height=${d[1] * k} viewBox="0 0 ${d[0]} ${d[1]}" aria-hidden=${label ? undefined : 'true'} role=${label ? 'img' : undefined} aria-label=${label}><use href="#g-${n}"/></svg>`;
}
// a redraw every ms while the page shows (none while hidden: battery)
function useTick(ms, on) {
	const [, n] = useState(0);
	useEffect(() => {
		if (on === false) return;
		let t = 0;
		const run = () => { clearInterval(t); t = document.hidden ? 0 : setInterval(() => n(x => x + 1), ms); if (!document.hidden) n(x => x + 1); };
		run(); document.addEventListener('visibilitychange', run);
		return () => { clearInterval(t); document.removeEventListener('visibilitychange', run); };
	}, [ms, on]);
}
function useMedia(q) {
	const [m, setM] = useState(matchMedia(q).matches);
	useEffect(() => { const mq = matchMedia(q), f = () => setM(mq.matches); mq.addEventListener('change', f); return () => mq.removeEventListener('change', f); }, [q]);
	return m;
}
// the local value while the user drags, the server's otherwise
function useHeld(serverVal) {
	const [held, setHeld] = useState(null), timer = useRef(0);
	const hold = v => { setHeld(v); clearTimeout(timer.current); timer.current = setTimeout(() => setHeld(null), 900); };
	return [held == null ? serverVal : held, hold];
}

function Modal({ title, sub, onClose, actions, children, cls }) {
	const ref = useRef();
	useEffect(() => {
		const prev = document.activeElement;
		const q = x => ref.current.querySelector(x);
		const f = q('[data-focus]') || q('input') || q('.sheet-item:not(:disabled)') || q('.btn.primary') || q('button');
		if (f) f.focus();
		return () => prev && prev.focus && prev.focus();
	}, []);
	const key = e => {
		if (e.key === 'Escape') { e.stopPropagation(); onClose(); }
		if (e.key === 'Tab') {
			const f = [...ref.current.querySelectorAll('button:not(:disabled),input,select')]; if (!f.length) return;
			const first = f[0], last = f[f.length - 1];
			if (e.shiftKey && document.activeElement === first) { e.preventDefault(); last.focus(); } else if (!e.shiftKey && document.activeElement === last) { e.preventDefault(); first.focus(); }
		}
	};
	return html`<div class="scrim" onMouseDown=${e => { if (e.target === e.currentTarget) onClose(); }} onKeyDown=${key}>
		<div class="modal ${cls || ''}" role="dialog" aria-modal="true" aria-label=${title} ref=${ref}>
			${title && html`<h2>${title}</h2>`}${sub && html`<div class="sub">${sub}</div>`}
			${children && html`<div class="body">${children}</div>`}
			${actions && html`<div class="acts">${actions}</div>`}
		</div></div>`;
}

function Seek({ view: v, onSeek, disabled }) {
	useTick(500);
	const ref = useRef(), [drag, setDrag] = useState(null);
	const dur = v.duration || 0, pos = drag != null ? drag : Math.min(v.pos(), dur || v.pos());
	const frac = dur ? Math.max(0, Math.min(1, pos / dur)) : 0;
	const at = e => { const r = ref.current.getBoundingClientRect(); return Math.max(0, Math.min(1, (e.clientX - r.left) / r.width)) * dur; };
	const down = e => { if (disabled || !dur) return; ref.current.setPointerCapture(e.pointerId); setDrag(at(e)); };
	const move = e => { if (drag != null) setDrag(at(e)); };
	const up = e => { if (drag != null) { const t = at(e); setDrag(null); onSeek(t); } };
	const key = e => {
		const d = { ArrowLeft: -5, ArrowRight: 10, ArrowDown: -5, ArrowUp: 10 }[e.key];
		if (d != null) { e.preventDefault(); e.stopPropagation(); onSeek(Math.max(0, Math.min(dur, pos + d))); }
	};
	return html`<div class="seek" ref=${ref} role="slider" tabindex=${disabled ? -1 : 0} aria-label="Seek" aria-valuemin="0" aria-valuemax=${Math.floor(dur)} aria-valuenow=${Math.floor(pos)}
		aria-valuetext=${fmtTime(pos) + ' of ' + fmtTime(dur)} aria-disabled=${disabled ? 'true' : undefined}
		onPointerDown=${down} onPointerMove=${move} onPointerUp=${up} onPointerCancel=${() => setDrag(null)} onKeyDown=${key}>
		<div class="track"><div class="fill" style="width:${frac * 100}%"></div>
			${dur > 0 && (v.chapters || []).slice(1).map(c => html`<i class="tick" style="left:${c.start / dur * 100}%"></i>`)}
			${dur > 0 && html`<div class="thumb" style="left:${frac * 100}%"></div>`}</div></div>`;
}

function Cover({ path, kind, k }) {
	const [bad, setBad] = useState(false);
	useEffect(() => setBad(false), [path]);
	if (path && !bad) return html`<img src="/api/cover?p=${encodeURIComponent(path)}" alt="" onError=${() => setBad(true)} />`;
	return html`<${Icon} n=${kind === 'video' ? 'film' : kind === 'photo' ? 'picture' : 'note'} k=${k || 4} />`;
}

function Banner({ title, sub, children, cls }) {
	return html`<div class="banner ${cls || ''}" role="status"><div class="grow"><div class="bt">${title}</div>${sub && html`<div class="bs">${sub}</div>`}</div>${children}</div>`;
}
function Empty({ icon, title, sub, button, onClick }) {
	return html`<div class="empty"><${Icon} n=${icon} k=${6} /><h2>${title}</h2><p>${sub}</p>${button && html`<button class="btn primary" onClick=${onClick}>${button}</button>`}</div>`;
}
const Skeleton = () => html`<div aria-hidden="true">${[1, 2, 3, 4, 5, 6].map(() => html`<div class="skel"></div>`)}</div>`;
function useLater(loading, ms) { const [s, setS] = useState(false); useEffect(() => { if (!loading) { setS(false); return; } const t = setTimeout(() => setS(true), ms); return () => clearTimeout(t); }, [loading]); return s; }

function MoreDots() { return html`<span class="dots" aria-hidden="true"><i></i><i></i><i></i></span>`; }

// a simple action sheet / menu: items [{label, icon, sub, danger, disabled, fn}]
function openSheet(title, glyph, items) {
	const close = openModal(close => html`<${Modal} cls="sheet" onClose=${close}>
		<div class="sheet-head"><${Icon} n=${glyph || 'note'} k=${3} cls="gl" /><div class="grow clamp2">${title}</div></div>
		<div style="padding:var(--s2) 0">${items.filter(Boolean).map(it => it === '-' ? html`<div class="sheet-sep" role="separator"></div>` : html`<button class="sheet-item ${it.danger ? 'danger' : ''}" disabled=${it.disabled} onClick=${() => { close(); it.fn && it.fn(); }}>
			${it.icon ? html`<${Icon} n=${it.icon} k=${2} cls=${it.y ? 'y' : ''} />` : html`<span style="width:24px"></span>`}<span class="grow">${it.label}${it.sub && html`<span class="sm">${it.sub}</span>`}</span>${it.on && html`<${Icon} n="check" k=${2} cls="on" label="Chosen" />`}</button>`)}</div></${Modal}>`);
	return close;
}

/* ================= login ================= */
async function afterLogin() {
	settle();
	set({ authed: true, signedOut: '', loginNote: '', signingIn: false, conn: 'ok', reconnecting: false, downReason: '', route: parseRoute() });
	C.okAt = Date.now();
	await refreshState(); openSSE(); loadExtras(); pingBoot();
}
// the player's start id, to tell a restart from a reconnect
async function pingBoot() { try { const { r } = await fetchT('/api/ping', { cache: 'no-store' }, 4000); C.boot = r.headers.get('X-BP-Boot') || C.boot; } catch (e) {} }
// a link with the PIN opened while the page is open: sign in with it, the PIN gone from the address
async function pinFromHash() {
	const m = location.hash.match(/^#pin=([A-Za-z0-9]{4})$/);
	replaceHist(null, location.pathname + location.search + (store.authed ? '#/playing' : ''));
	if (!m) return;
	if (store.authed) { set({ route: parseRoute() }); return; }
	set({ authed: false, signingIn: true, route: parseRoute() });
	try { await post('/api/login', { pin: m[1] }); await afterLogin(); }
	catch (e) { set({ signingIn: false, loginNote: e.status === 401 ? 'This QR code is out of date. Enter the PIN shown on your handheld.' : errMsg(e) }); }
}
async function loadExtras() {
	try { set({ eq: (await get('/api/eq')).presets }); } catch (e) {}
	try { const r = (await get('/api/roots')).roots; set({ roots: r }); } catch (e) {}
}

function Login() {
	const s = useStore();
	const [chars, setChars] = useState(['', '', '', '']), [status, setStatus] = useState('idle'), [msg, setMsg] = useState(''), [lockEnd, setLockEnd] = useState(0), [shake, setShake] = useState(0);
	const refs = useRef([]);
	useTick(1000, !!lockEnd);
	const left = lockEnd ? Math.max(0, Math.ceil((lockEnd - Date.now()) / 1000)) : 0;
	useEffect(() => { if (lockEnd && !left) { setLockEnd(0); setStatus('idle'); setMsg(''); setTimeout(() => refs.current[0] && refs.current[0].focus(), 0); } });
	useEffect(() => { if (!s.signingIn && refs.current[0]) refs.current[0].focus(); }, [s.signingIn]);
	const locked = !!lockEnd && left > 0;
	async function submit(arr) {
		setStatus('busy'); setMsg('Signing in…');
		try { await post('/api/login', { pin: arr.join('') }); await afterLogin(); }
		catch (e) {
			const b = e.body || {};
			setChars(['', '', '', '']);
			if (b.retryAfter > 0) { setLockEnd(Date.now() + b.retryAfter * 1000); setStatus('locked'); setMsg(''); return; }
			if (e.status === 401) { setStatus('wrong'); setShake(x => x + 1); setMsg("That's not the PIN. " + plural(b.left == null ? 0 : b.left, 'try', 'tries') + ' left.'); }
			else { setStatus('idle'); setMsg(errMsg(e)); }
			setTimeout(() => refs.current[0] && refs.current[0].focus(), 0);
		}
	}
	const clean = v => v.toUpperCase().replace(/[^A-Z0-9]/g, '');
	function input(i, e) {
		const v = clean(e.target.value);
		if (!v) { const a = chars.slice(); a[i] = ''; setChars(a); return; }
		const a = chars.slice(); let j = i;
		for (const c of v) { if (j < 4) a[j++] = c; }
		setChars(a); setStatus('idle'); setMsg('');
		const next = Math.min(j, 3);
		if (refs.current[next]) refs.current[next].focus();
		if (a.every(Boolean)) submit(a);
	}
	function keydown(i, e) {
		if (e.key === 'Backspace' && !chars[i] && i > 0) { e.preventDefault(); const a = chars.slice(); a[i - 1] = ''; setChars(a); refs.current[i - 1].focus(); }
		if (e.key === 'ArrowLeft' && i > 0) refs.current[i - 1].focus();
		if (e.key === 'ArrowRight' && i < 3) refs.current[i + 1].focus();
	}
	function paste(e) { e.preventDefault(); const v = clean((e.clipboardData || window.clipboardData).getData('text')).slice(0, 4); if (!v) return; const a = ['', '', '', '']; [...v].forEach((c, i) => a[i] = c); setChars(a); if (a.every(Boolean)) submit(a); else refs.current[v.length].focus(); }
	const busy = status === 'busy' || s.signingIn;
	const dis = busy || locked;
	return html`<main class="login">
		<img src="/logo.svg" alt="" />
		<h1>Banana Player</h1>
		${s.signedOut === 'pin' && html`<${Banner} cls="info" title="Signed out: a new PIN was made on your handheld." />`}
		${s.signedOut === 'expired' && html`<${Banner} cls="info" title="You were signed out. Enter the PIN to continue." />`}
		${s.loginNote && html`<${Banner} cls="info" title=${s.loginNote} />`}
		<p class="sub">Enter the PIN shown on your handheld</p>
		<div class="pin ${status === 'wrong' ? 'wrong' : ''} ${busy ? 'busy' : ''} ${shake && status === 'wrong' ? 'shake' : ''}" key=${shake} role="group" aria-label="PIN">
			${chars.map((c, i) => html`<input ref=${r => refs.current[i] = r} value=${c} disabled=${dis} inputmode="text" autocomplete=${i ? 'off' : 'one-time-code'} autocapitalize="characters" autocorrect="off" spellcheck="false" maxlength=${i === 0 ? 4 : 2}
				aria-label=${'PIN character ' + (i + 1)} onInput=${e => input(i, e)} onKeyDown=${e => keydown(i, e)} onPaste=${paste} onFocus=${e => e.target.select()} />`)}
		</div>
		<div class="msg ${status === 'wrong' || locked ? 'err' : 'dim'}" aria-live="polite">${s.signingIn ? 'Signing in…' : locked ? 'Too many tries. Try again in ' + fmtTime(left) + '.' : msg}</div>
		<p class="help">On your handheld: START › Settings › Web dashboard</p>
		<p class="foot">Unencrypted: use it on your home Wi-Fi</p>
	</main>`;
}

/* ================= now playing =================
   YouTube Music's shape: the media fills the space; one transport row; everything else (Queue, Chapters, Sound or
   Audio & Subtitles) in one tabbed panel: a bottom sheet on phones and tablets, a column on the right on desktop,
   where the bottom bar carries the transport. Phone and tablet show it as a full-screen player that the mini player
   expands into and a chevron or a swipe down folds away. */
const reduceMotion = () => matchMedia('(prefers-reduced-motion: reduce)').matches;
const playableCache = {};
function useCanPlayHere(path) {
	const [r, setR] = useState(null);
	useEffect(() => {
		if (!path) { setR(null); return; }
		if (playableCache[path]) { setR(playableCache[path]); return; }
		let dead = false;
		probe1(path).then(m => { const ok = m ? probeOK(m) : true; playableCache[path] = ok ? 'yes' : 'no'; if (!dead) setR(playableCache[path]); });
		return () => { dead = true; };
	}, [path]);
	return r;
}
// elapsed on the left; on the right the time left ("−0:57"), or the length: a tap swaps them. skip: −5s / +10s between
function Times({ v, skip, off }) {
	useTick(1000);
	const [total, setTotal] = useState(lsGet('bpTimeTotal') === '1');
	const p = v.pos(), d = v.duration;
	const flip = () => { lsSet('bpTimeTotal', total ? '0' : '1'); setTotal(!total); };
	return html`<div class="times"><span>${fmtTime(p)}</span>
		${skip && html`<span class="skips"><button aria-label="Back 5 seconds" onClick=${() => ctl.seekRel(-5)} disabled=${off}>−5s</button><button aria-label="Forward 10 seconds" onClick=${() => ctl.seekRel(10)} disabled=${off}>+10s</button></span>`}
		${d > 0 ? html`<button class="tm-r" onClick=${flip} aria-label=${total ? 'Length ' + fmtTime(d) + '. Show the time left' : fmtTime(d - p) + ' left. Show the length'}>${total ? fmtTime(d) : '−' + fmtTime(d - p)}</button>` : html`<span></span>`}</div>`;
}

/* ---- where it plays: one chip, a chooser like a cast picker ---- */
const targetLabel = t => t === 'browser' ? 'In this browser' : 'On handheld';
function chooseDevice() {
	const sn = store.snap, t = store.target;
	const noHere = t === 'handheld' && !B.active && sn && sn.path && playableCache[sn.path] === 'no';
	openSheet('Play on', 'speaker', [
		{ label: 'On handheld', icon: 'card', sub: "Your handheld's speakers or headphones", on: t === 'handheld', fn: () => setTarget('handheld') },
		{ label: 'In this browser', icon: 'laptop', sub: NO_HERE ? NO_HERE_WHY : noHere ? 'Plays on the handheld only' : 'This phone or computer', on: t === 'browser', disabled: NO_HERE || noHere, fn: () => setTarget('browser') },
	]);
}
function DeviceChip({ cls }) {
	const t = store.target;
	return html`<button class=${'devchip ' + (cls || '')} aria-haspopup="dialog" aria-label=${targetLabel(t) + '. Change where it plays'} onClick=${chooseDevice}>
		<${Icon} n=${t === 'browser' ? 'laptop' : 'card'} k=${2} /><span class="ell">${targetLabel(t)}</span><${Icon} n="down" k=${1} cls="caret" /></button>`;
}

// "English" from "English · SRT", "Track 2" from "Track 2 · SRT"
const shortLabel = l => (l || '').split(' · ')[0];
function subChip(v) {
	if (v.here) { const s = SUBS.list.find(x => x.i === SUBS.chosen); return s ? shortLabel(s.label) : 'Off'; }
	const t = v.tracks; if (!t) return null;
	const s = (t.sub || []).find(x => x.selected); return s ? shortLabel(s.label) : 'Off';
}
/* the sheet and the full player answer Back (Android's button, iOS's swipe, the browser's): each open layer is a
   history entry of its own, with the same address, so the hash routes don't move */
const compactNow = () => !matchMedia('(min-width: 1024px)').matches;
function openPanel(extra) {
	if (!store.sheetOpen && compactNow() && !(history.state && history.state.sheet)) pushHist({ sheet: 1 });
	set(Object.assign({ sheetOpen: true }, extra));
}
function closePanel() {
	if (!store.sheetOpen) return;
	set({ sheetOpen: false });
	if (history.state && history.state.sheet) history.back();
}
addEventListener('popstate', e => { if (store.sheetOpen && !(e.state && e.state.sheet)) set({ sheetOpen: false }); });
// the panel's tab, opened (the sheet slides up on a phone)
function showTab(tab, extra) {
	openPanel(Object.assign({ npTab: tab }, extra));
	requestAnimationFrame(() => { const c = document.querySelector('.np-panel .tabbody, .sheet2 .tabbody'); if (c) c.scrollTop = 0; });
}
const showTracks = tab => showTab('tracks', { tracksTab: tab });
function npTabs(v) {
	const vid = v.kind === 'video', t = [['queue', 'Queue']];
	if (v.chapters.length) t.push(['chapters', 'Chapters']);
	if (vid) t.push(['tracks', 'Tracks']);	// (short: four tabs fit the desktop panel; the tab's own switch says Audio / Subtitles)
	else { t.push(['sound', 'Sound']); if (v.tracks && (v.tracks.audio || []).length > 1) t.push(['tracks', 'Tracks']); }
	t.push(['details', 'Details']);
	return t;
}
function TabBody({ v, tab }) {
	return html`<div class="tabpane" key=${tab}>${TabBody1({ v, tab })}</div>`;
}
function TabBody1({ v, tab }) {
	return tab === 'details' ? html`<${DetailsTab} v=${v} />` : tab === 'chapters' ? html`<${ChaptersCard} v=${v} />` : tab === 'tracks' ? html`<${TracksCard} v=${v} />`
		: tab === 'sound' ? (v.here ? html`<${SoundHere} />` : html`<${SoundCard} v=${v} />`) : html`<${UpNext} v=${v} />`;
}
function PanelTabs({ tabs, tab, open, onTab }) {
	return html`<div class="ptabs" role="tablist" aria-label="More about what's playing">${tabs.map(([k, l]) => html`<button role="tab" id=${'pt-' + k} aria-controls="np-tabbody" aria-selected=${open !== false && tab === k} onClick=${() => onTab(k)}>${l}</button>`)}</div>`;
}

/* ---- the media: the video here, the cover, or the handheld's video as a poster; taps as on YouTube ---- */
function useFullscreen() {
	const [fs, setFs] = useState(null);
	useEffect(() => { const f = () => setFs(document.fullscreenElement || document.webkitFullscreenElement || null); document.addEventListener('fullscreenchange', f); document.addEventListener('webkitfullscreenchange', f); return () => { document.removeEventListener('fullscreenchange', f); document.removeEventListener('webkitfullscreenchange', f); }; }, []);
	return fs;
}
function toggleFullscreen(box) {
	const cur = document.fullscreenElement || document.webkitFullscreenElement;
	if (cur) { (document.exitFullscreen || document.webkitExitFullscreen).call(document); return; }
	if (box.requestFullscreen) box.requestFullscreen().catch(() => {});
	else if (box.webkitRequestFullscreen) box.webkitRequestFullscreen();
	else if (el.webkitEnterFullscreen) el.webkitEnterFullscreen();	// iPhone: its own player, which keeps the subtitle tracks
}
// a track that changed while the page shows: in from the right (Next) or the left (Previous), else a fade
const seenTrack = { p: null };
function trackClass(p) {
	if (seenTrack.p === null) { seenTrack.p = p; return ''; }
	if (seenTrack.p !== p) { seenTrack.p = p; seenTrack.cls = Date.now() - trackDirAt < 4000 ? (trackDir < 0 ? 'trk-prev' : 'trk-next') : 'trk-fade'; seenTrack.at = Date.now(); }
	return Date.now() - (seenTrack.at || 0) < 600 ? seenTrack.cls || '' : '';
}
function Media({ v, off, immersive, onCollapse }) {
	const box = useRef();
	// the one media element shows in the slot. It goes back to #holder as the slot leaves, before Preact takes the slot
	// out (a ref is let go first; an effect's cleanup comes after): a media element out of the document is paused
	const slot = useCallback(n => {
		if (n) { n.prepend(el); sizeCues(); }
		else if (el.parentNode && el.parentNode.id !== 'holder') document.getElementById('holder').appendChild(el);
	}, []);
	const live = v.here && v.kind === 'video';
	const fsEl = useFullscreen(), fs = !!fsEl && fsEl === box.current;
	const full = immersive || fs;
	const [ov, setOv] = useState(false), [pulse, setPulse] = useState(null), [rip, setRip] = useState(null);
	const t = useRef({ last: 0, side: '', single: 0, hide: 0, n: 0 });
	useEffect(() => () => { clearTimeout(t.current.single); clearTimeout(t.current.hide); }, []);
	// the overlay fades out after 3 s while it plays; it stays while paused
	const showOv = () => { setOv(true); clearTimeout(t.current.hide); t.current.hide = setTimeout(() => { if (!view().paused) setOv(false); }, 3000); };
	const toggle = () => { if (off) return; const was = view().paused; ctl.toggle(); setPulse({ k: was ? 'play' : 'pause', id: ++t.current.n }); if (ov) showOv(); };
	const seek = (side) => { if (off) return; ctl.seekRel(side === 'l' ? -5 : 10); setRip(r => ({ side, id: ++t.current.n, n: r && r.side === side && Date.now() - r.at < 900 ? r.n + 1 : 1, at: Date.now() })); };
	const up = e => {
		if (e.button > 0 || e.target.closest('button,.seek') || box.current.dataset.swiped) return;
		const r = box.current.getBoundingClientRect(), x = (e.clientX - r.left) / r.width;
		const side = x < 1 / 3 ? 'l' : x > 2 / 3 ? 'r' : 'c', tt = t.current, now = Date.now();
		// a mouse: a click plays or pauses at once, a double click is fullscreen (YouTube on a computer)
		if (e.pointerType === 'mouse') { if (now - tt.last < 300 && live) { tt.last = 0; toggleFullscreen(box.current); return; } tt.last = now; toggle(); return; }
		// a finger: a double tap on the left or right third seeks; a single tap shows the overlay over a video, and is
		// play / pause on a cover (YouTube on a phone)
		if (now - tt.last < 300 && side !== 'c' && side === tt.side) { clearTimeout(tt.single); tt.last = now; seek(side); return; }
		tt.last = now; tt.side = side;
		clearTimeout(tt.single);
		// a run of seeks keeps going without waiting
		if (rip && now - rip.at < 700 && side === rip.side) { seek(side); return; }
		tt.single = setTimeout(() => { if (live) { if (ov && !view().paused) setOv(false); else showOv(); } else toggle(); }, 260);
	};
	const move = e => { if (live && e.pointerType === 'mouse') showOv(); };
	const shown = live && (ov || v.paused);
	const cover = v.kind === 'video' ? v.path : v.cover;
	return html`<div class=${'media ' + (live ? 'is-video' : v.kind === 'video' ? 'is-poster' : 'is-cover') + (full ? ' full' : '') + (shown ? ' ov' : '')} ref=${box}
		onPointerUp=${up} onPointerMove=${move} onMouseLeave=${() => { if (!view().paused) setOv(false); }} role="group" aria-label=${v.title}>
		${live ? html`<div class="mvideo" ref=${slot}></div>` : html`<div class=${'mart ' + trackClass(v.path)} key=${v.path}><${Cover} path=${cover} kind=${v.kind} k=${v.kind === 'video' ? 6 : 8} /></div>`}
		${!live && v.kind === 'video' && html`<div class="poster-tag"><${Icon} n="card" k=${2} />Playing on your handheld</div>`}
		${!live && v.kind === 'video' && v.paused && !v.loading && html`<div class="mpaused" aria-hidden="true"><${Icon} n="play" k=${5} /></div>`}
		${live && html`<div class="mov" aria-hidden=${shown ? undefined : 'true'}>
			${full && html`<div class="mov-top">
				${immersive && !fs ? html`<button class="ib" aria-label="Close player" onClick=${onCollapse}><${Icon} n="down" k=${2} /></button>` : html`<span style="width:12px"></span>`}
				<div class="grow"><div class="mov-t ell">${v.title}</div>${v.artist && html`<div class="mov-a ell">${v.artist}</div>`}</div>
				${subChip(v) != null && html`<button class="ib" aria-label=${'Subtitles: ' + subChip(v)} aria-pressed=${subChip(v) !== 'Off'} onClick=${() => { if (fs) toggleFullscreen(box.current); showTracks('sub'); }}><${Icon} n="cc" k=${2} /></button>`}</div>`}
			<div class="mov-mid">
				${full && html`<button class="mov-s" aria-label="Previous" onClick=${ctl.prev} disabled=${off}><${Icon} n="prev" k=${3} /></button>`}
				<button class="mov-play" aria-label=${v.paused ? 'Play' : 'Pause'} onClick=${toggle} disabled=${off}><${Icon} n=${v.paused ? 'play' : 'pause'} k=${4} /></button>
				${full && html`<button class="mov-s" aria-label="Next" onClick=${ctl.next} disabled=${off}><${Icon} n="next" k=${3} /></button>`}</div>
			<div class="mov-bot">
				${full && html`<div class="mov-seek"><${Seek} view=${v} onSeek=${ctl.seek} disabled=${off} /><${Times} v=${v} /></div>`}
				<button class="ib mov-fs" aria-label=${fs ? 'Leave fullscreen' : 'Fullscreen'} onClick=${() => toggleFullscreen(box.current)}><${Icon} n=${fs ? 'fullscreen-exit' : 'fullscreen'} k=${2} /></button></div></div>`}
		${pulse && html`<div class="mpulse" key=${pulse.id} aria-hidden="true"><${Icon} n=${pulse.k} k=${4} /></div>`}
		${rip && html`<div class=${'mrip ' + rip.side} key=${rip.id} aria-hidden="true"><span>${rip.side === 'l' ? '−' + 5 * rip.n + ' s' : '+' + 10 * rip.n + ' s'}</span></div>`}
	</div>`;
}

/* ---- the title and what it's from; on a phone or a tablet the small actions are a row of their own (NpActs) ---- */
// over the scrubber, so a long title has the whole width: the heart on the left, subtitles and the volume on the right
function NpActs({ v }) {
	const chip = v.kind === 'video' ? subChip(v) : null;
	return html`<div class="np-arow">
		${likable(v) && html`<${LikeBtn} v=${v} cls="np-ib" />`}<span class="grow"></span>
		${chip != null && html`<button class=${'np-ib' + (chip !== 'Off' ? ' on' : '')} aria-label=${'Subtitles: ' + chip} title=${'Subtitles: ' + chip} onClick=${() => showTracks('sub')}><${Icon} n="cc" k=${2} /></button>`}
		<${VolButton} /></div>`;
}
// the full player's ⋯: what to do with what plays, then the app's own
function npMore(v) {
	openSheet(v.title, v.kind === 'video' ? 'film' : 'note', [
		likable(v) && { label: 'Add to playlist…', icon: 'pl-add', fn: () => addToPlaylist([v.path], v.title) },
		v.path && { label: 'Show in Files', icon: 'folder', fn: () => showInFiles(v.path) },
		(likable(v) || v.path) && '-',
		{ label: 'Keyboard shortcuts', icon: 'info', fn: () => set({ help: true }) }, { label: 'Sign out', icon: 'power', fn: signOut }]);
}
function NpMeta({ v, desk }) {
	const vid = v.kind === 'video', chip = vid ? subChip(v) : null;
	const src = v.source && v.source.kind && v.source.kind !== 'none' ? v.source : null;
	return html`<div class="np-meta">
		<div class="np-tl">
			<div class=${'grow ' + trackClass(v.path)} key=${v.path}><div class="np-title clamp2">${v.title}</div>
				${(v.artist || v.album) && html`<div class="np-artist ell">${[v.artist, v.album].filter(Boolean).join(' · ')}</div>`}</div>
			</div>
		${(src || v.chapters.length > 0) && html`<div class="np-lines">
			${src && html`<div class="np-source"><${Icon} n=${src.kind === 'playlist' ? 'playlist' : 'folder'} k=${2} /><span class="ell">From ${src.title}${src.count ? ' · ' + (src.index + 1) + ' / ' + src.count : ''}</span></div>`}
			${v.chapters.length > 0 && html`<button class="np-chap" onClick=${() => showTab('chapters')}><${Icon} n="chapters" k=${2} /><span class="ell">Chapter ${Math.max(1, v.chapter + 1)} of ${v.chapters.length}${v.chapter >= 0 ? ' · ' + chapterTitle(v.chapters[v.chapter], v.chapter) : ''}</span></button>`}</div>`}
		${desk && chip != null && html`<div class="np-row">${html`<button class=${'pillbtn' + (chip !== 'Off' ? ' on' : '')} onClick=${() => showTracks('sub')}><${Icon} n="cc" k=${2} /><span class="ell">Subtitles: ${chip}</span></button>`}</div>`}
	</div>`;
}
function LoopIcon({ loop, k }) {
	return html`<span class="loopic"><${Icon} n="loop" k=${k} />${loop === 'one' && html`<b>1</b>`}</span>`;
}
const loopName = l => 'Loop: ' + (l === 'one' ? 'One' : l === 'all' ? 'All' : 'Off');
function Transport({ v, off }) {
	return html`<div class="tp">
		<button class=${'tp-s' + (v.shuffled ? ' on' : '')} aria-label="Shuffle" title=${v.shuffled ? 'Shuffle: On' : 'Shuffle: Off'} aria-pressed=${!!v.shuffled} onClick=${ctl.shuffle} disabled=${off}><${Icon} n="shuffle" k=${2} /></button>
		<button class="tp-m" aria-label="Previous" onClick=${ctl.prev} disabled=${off}><${Icon} n="prev" k=${3} /></button>
		<button class="tp-play" aria-label=${v.paused ? 'Play' : 'Pause'} onClick=${ctl.toggle} disabled=${off}><${Icon} n=${v.paused ? 'play' : 'pause'} k=${4} /></button>
		<button class="tp-m" aria-label="Next" onClick=${ctl.next} disabled=${off}><${Icon} n="next" k=${3} /></button>
		<button class=${'tp-s' + (v.loop !== 'no' ? ' on' : '')} aria-label=${loopName(v.loop)} title=${loopName(v.loop)} aria-pressed=${v.loop !== 'no'} onClick=${ctl.loop} disabled=${off}><${LoopIcon} loop=${v.loop} k=${2} /></button></div>`;
}
// the volume as a speaker button that opens a small slider
function VolButton() {
	const [open, setOpen] = useState(false), ref = useRef();
	useEffect(() => {
		if (!open) return;
		const f = e => { if (ref.current && !ref.current.contains(e.target)) setOpen(false); };
		const k = e => { if (e.key === 'Escape') { e.stopPropagation(); setOpen(false); } };
		document.addEventListener('pointerdown', f, true); document.addEventListener('keydown', k, true);
		return () => { document.removeEventListener('pointerdown', f, true); document.removeEventListener('keydown', k, true); };
	}, [open]);
	return html`<div class="volwrap" ref=${ref}><button class=${'np-ib' + (open ? ' on' : '')} aria-label="Volume" aria-expanded=${open} onClick=${() => setOpen(!open)}><${Icon} n="speaker" k=${2} /></button>
		${open && html`<div class="volpop" role="group" aria-label="Volume"><${VolumeRow} /></div>`}</div>`;
}

function Strips({ v, other, sn }) {
	return html`${v.here && v.parked != null && html`<div class="strip"><span>Stopped when the connection to your handheld dropped.</span><button class="btn sm primary" onClick=${playParked}><${Icon} n="play" k=${2} />Play here from ${fmtTime(v.parked)}</button></div>`}
		${other && !NO_HERE && html`<div class="strip"><span>Your handheld is playing <b>${titleFor(sn.path, sn.title)}</b></span><button class="btn sm primary" onClick=${continueHere}>Continue here</button></div>`}`;
}

function NowPlaying() {
	const s = useStore(), v = view(), sn = s.snap;
	const desk = useMedia('(min-width: 1024px)');
	useCanPlayHere(s.target === 'handheld' && sn ? sn.path : null);
	const off = s.reconnecting && !v.here;
	// the handheld plays something this page doesn't have: offer it here
	const other = s.target === 'browser' && sn && sn.path && sn.kind !== 'photo' && (!B.meta || (sn.path !== B.meta.p && !sn.paused));
	if (!v.has) return html`<div class="np idle">
		<div class="np-idlehead"><${DeviceChip} /></div>
		<${Strips} v=${v} other=${other} sn=${sn} />
		<${Empty} icon="note" title="Nothing playing" sub="Pick something in Files or Playlists." button="Open Files" onClick=${() => go(filesHash(store.lastDir || ''))} /></div>`;
	const tabs = npTabs(v), tab = tabs.some(x => x[0] === s.npTab) ? s.npTab : 'queue';
	if (desk) return html`<div class=${'np desk' + (v.kind === 'video' ? ' vid' : '')}>
		<section class="np-stage" aria-label="Now playing"><div class="stage-in">
			<${Strips} v=${v} other=${other} sn=${sn} />
			<${Media} v=${v} off=${off} />
			<${NpMeta} v=${v} desk=${true} /></div></section>
		<aside class="np-panel" aria-label="Queue and more"><${PanelTabs} tabs=${tabs} tab=${tab} onTab=${k => set({ npTab: k })} />
			<div class="tabbody" id="np-tabbody" role="tabpanel" aria-labelledby=${'pt-' + tab}><${TabBody} v=${v} tab=${tab} /></div></aside></div>`;
	return html`<${FullPlayer} v=${v} off=${off} other=${other} sn=${sn} tabs=${tabs} tab=${tab} />`;
}

// the phone's and the tablet's player: it fills the screen; the panel is a sheet that peeks at the bottom
function FullPlayer({ v, off, other, sn, tabs, tab }) {
	const s = store, root = useRef();
	const short = useMedia('(orientation: landscape) and (max-height: 520px)');
	const immersive = short && v.here && v.kind === 'video';
	const [leaving, setLeaving] = useState(false);
	const entering = useMemo(() => Date.now() - (s.npAnim || 0) < 600 && store.vtKind !== 'player-open', []);
	useEffect(() => {
		if (store.backNav || !tapped()) return;	// (not tapped: Safari would skip the entry under it; the chevron replaces instead)
		// opened straight at #/playing: Files goes under it, so Back folds the player instead of leaving the app
		const under = filesHash(store.lastDir || '');
		replaceHist(null, under); pushHist(null, '#/playing');
		set({ backHash: under, backNav: true });
	}, []);
	const collapse = () => {
		if (leaving) return;
		const sheet = history.state && history.state.sheet ? 1 : 0;
		if (s.sheetOpen) set({ sheetOpen: false });
		// back when what's behind is ours; else the page under it takes the player's place (no load: it keeps playing)
		const leave = () => { if (store.backNav && canGoBack(1 + sheet)) history.go(-(1 + sheet)); else replaceHash(store.backHash || filesHash(store.lastDir || '')); };
		if (reduceMotion() || (document.startViewTransition && !videoHere())) { leave(); return; }	// (a View Transition morphs it back)
		setLeaving(true); setTimeout(leave, 260);
	};
	// a swipe down on the header or the media folds the player away; it follows the finger
	const sw = useRef(null);
	const down = e => {
		if (e.pointerType === 'mouse' || e.target.closest('.seek,.sheet2,.np-ctl,input,select,.volpop') || immersive) return;
		sw.current = { y: e.clientY, x: e.clientX, t: Date.now(), dy: 0, on: false, box: e.target.closest('.media') };
	};
	const move = e => {
		const w = sw.current; if (!w) return;
		const dy = e.clientY - w.y, dx = e.clientX - w.x;
		if (!w.on && dy > 12 && dy > Math.abs(dx) * 1.5) { w.on = true; root.current.classList.add('dragging'); if (w.box) w.box.dataset.swiped = '1'; }
		if (w.on) { w.dy = Math.max(0, dy); root.current.style.transform = `translateY(${w.dy}px)`; }
	};
	const up = () => {
		const w = sw.current; sw.current = null; if (!w || !w.on) return;
		root.current.classList.remove('dragging');
		setTimeout(() => { if (w.box) delete w.box.dataset.swiped; }, 0);
		const fast = w.dy / Math.max(1, Date.now() - w.t) > .5;
		if (w.dy > 140 || (fast && w.dy > 40)) { root.current.style.transform = ''; collapse(); }
		else root.current.style.transform = '';
	};
	return html`<div class=${'fp' + (entering ? ' entering' : '') + (leaving ? ' leaving' : '') + (short ? ' short' : '') + (immersive ? ' immersive' : '') + (v.kind === 'video' ? ' vid' : '')} ref=${root}
		onPointerDown=${down} onPointerMove=${move} onPointerUp=${up} onPointerCancel=${up}>
		<header class="fp-head">
			<button class="ib" aria-label="Close player" onClick=${collapse}><${Icon} n="down" k=${2} /></button>
			<div class="grow fp-mid"><${DeviceChip} /></div>
			<button class="ib" aria-label="More" aria-haspopup="dialog" onClick=${() => npMore(v)}><${MoreDots} /></button></header>
		<${Strips} v=${v} other=${other} sn=${sn} />
		<section class="fp-body" aria-label="Now playing">
			<div class="fp-media"><${Media} v=${v} off=${off} immersive=${immersive} onCollapse=${collapse} /></div>
			<div class="np-ctl">
				<${NpMeta} v=${v} />
				<${NpActs} v=${v} />
				<div class="np-seek"><${Seek} view=${v} onSeek=${ctl.seek} disabled=${off} /><${Times} v=${v} skip=${true} off=${off} /></div>
				<${Transport} v=${v} off=${off} /></div></section>
		<${Sheet} v=${v} tabs=${tabs} tab=${tab} hidden=${immersive && !s.sheetOpen} />
	</div>`;
}

// the bottom sheet: peeks with its tabs; a tab or a drag up opens it, a drag down or the row on top closes it
function Sheet({ v, tabs, tab, hidden }) {
	const s = store, open = !!s.sheetOpen, ref = useRef(), d = useRef(null);
	const setOpen = o => o ? openPanel() : closePanel();
	const peekOffset = () => { const el2 = ref.current; return el2 ? el2.offsetHeight - el2.querySelector('.sh-peek').offsetHeight : 0; };
	const down = e => {
		if (e.button > 0) return;
		d.current = { y: e.clientY, t: Date.now(), base: open ? 0 : peekOffset(), on: false, cur: 0 };
	};
	const move = e => {
		const g = d.current; if (!g) return;
		const dy = e.clientY - g.y;
		if (!g.on && Math.abs(dy) > 8) { g.on = true; ref.current.classList.add('dragging'); try { ref.current.setPointerCapture(e.pointerId); } catch (x) {} }
		if (g.on) { g.cur = Math.max(0, Math.min(peekOffset(), g.base + dy)); ref.current.style.transform = `translateY(${g.cur}px)`; e.preventDefault(); }
	};
	const up = e => {
		const g = d.current; d.current = null; if (!g || !g.on) return;
		ref.current.classList.remove('dragging'); ref.current.style.transform = '';
		g.dragged = true; sheetDragged = Date.now();
		const v2 = (e.clientY - g.y) / Math.max(1, Date.now() - g.t), mid = peekOffset() / 2;
		setOpen(Math.abs(v2) > .4 ? v2 < 0 : g.cur < mid);
	};
	// a tab opens the sheet there; the one already showing closes it
	const onTab = k => { if (Date.now() - sheetDragged < 250) return; if (open && k === tab) closePanel(); else openPanel({ npTab: k }); };
	return html`<section class=${'sheet2' + (open ? ' open' : '') + (hidden ? ' gone' : '')} ref=${ref} aria-label="Queue and more">
		<div class="sh-peek" onPointerDown=${down} onPointerMove=${move} onPointerUp=${up} onPointerCancel=${up}>
			<div class="sh-np" aria-hidden=${open ? undefined : 'true'}>
				<button class="sh-np-main" tabindex=${open ? 0 : -1} aria-label="Back to the player" onClick=${() => { if (Date.now() - sheetDragged > 250) setOpen(false); }}>
					<span class="art-sm"><${Cover} path=${v.kind === 'video' ? v.path : v.cover} kind=${v.kind} k=${3} /></span>
					<span class="grow"><span class="t ell">${v.title}</span><span class="a ell">${v.artist || targetLabel(store.target)}</span></span></button>
				<button class="ib play" tabindex=${open ? 0 : -1} aria-label=${v.paused ? 'Play' : 'Pause'} onClick=${ctl.toggle}><${Icon} n=${v.paused ? 'play' : 'pause'} k=${3} /></button>
				<button class="ib sh-close" tabindex=${open ? 0 : -1} aria-label="Close" onClick=${closePanel}><${Icon} n="down" k=${2} /></button></div>
			<button class="sh-grip" aria-label=${open ? 'Close the panel' : 'Open the panel'} aria-expanded=${open} onClick=${() => { if (Date.now() - sheetDragged > 250) setOpen(!open); }}><i></i></button>
			<${PanelTabs} tabs=${tabs} tab=${tab} open=${open} onTab=${onTab} /></div>
		<div class="tabbody" id="np-tabbody" role="tabpanel" aria-labelledby=${'pt-' + tab} inert=${open ? undefined : true}><${TabBody} v=${v} tab=${tab} /></div>
	</section>`;
}
let sheetDragged = 0;

/* ---- what the handheld reads from the file ---- */
const FORMATS = { matroska: 'Matroska (MKV)', 'matroska,webm': 'Matroska (MKV)', webm: 'WebM', mov: 'MP4', 'mov,mp4,m4a,3gp,3g2,mj2': 'MP4', mp3: 'MP3', flac: 'FLAC', ogg: 'Ogg', wav: 'WAV', avi: 'AVI', asf: 'ASF (WMA / WMV)', aac: 'AAC', ape: 'APE', mpeg: 'MPEG', mpegts: 'MPEG-TS' };
const CODECS = { h264: 'H.264', hevc: 'H.265 (HEVC)', vp8: 'VP8', vp9: 'VP9', av1: 'AV1', mpeg4: 'MPEG-4', mpeg2video: 'MPEG-2', aac: 'AAC', mp3: 'MP3', mp3float: 'MP3', flac: 'FLAC', opus: 'Opus', vorbis: 'Vorbis', ac3: 'Dolby Digital (AC-3)', eac3: 'Dolby Digital Plus', dts: 'DTS', truehd: 'Dolby TrueHD', alac: 'ALAC', wmav2: 'WMA', ape: 'APE' };
const codecName = c => !c ? '' : CODECS[c] || (/^pcm_/.test(c) ? 'PCM' : c.toUpperCase());
const formatName = (c, p) => FORMATS[c] || (c ? c.split(',')[0].toUpperCase() : (p.split('.').pop() || '').toUpperCase());
const chName = n => n === 1 ? 'Mono' : n === 2 ? 'Stereo' : n === 6 ? '5.1' : n === 8 ? '7.1' : n ? n + ' channels' : '';
// what /api/media says (tags, format, codecs; more as the server sends it), with the size and date from the folder's list
function DetailsTab({ v }) {
	const p = v.path, [m, setM] = useState(null), [row, setRow] = useState(null);
	useEffect(() => {
		let dead = false;
		const known = v.here && B.meta && B.meta.p === p && B.meta.media;
		setM(known || null); setRow(null);
		if (!known) get('/api/media', { p }, { ms: 8000, quiet: true }).then(x => { if (!dead) setM(x || {}); }).catch(() => { if (!dead) setM({}); });
		get('/api/ls', { p: dirName(p) }, { quiet: true }).then(r => { if (!dead) setRow((r.items || []).find(i => i.name === baseName(p)) || {}); }).catch(() => { if (!dead) setRow({}); });
		return () => { dead = true; };
	}, [p, store.epoch]);
	if (!m) return html`<div aria-hidden="true">${[1, 2, 3, 4].map(() => html`<div class="skel" style="height:40px"></div>`)}</div>`;
	const vid = v.kind === 'video', segs = dirName(p).split('/').filter(Boolean);
	const join = (...a) => a.filter(Boolean).join(' · ');
	// "3/12" as the tag has it: "3 of 12"
	const ofN = x => x ? String(x).replace(/^\s*0*(\d+)\s*\/\s*0*(\d+)\s*$/, '$1 of $2') : '';
	const kbps = b => b ? Math.round(b / 1000).toLocaleString() + ' kbps' : '';
	const res = m.width && m.height ? m.width + ' × ' + m.height : '';
	const rows = [
		['Title', v.title], ['Artist', m.artist || v.artist], ['Album artist', m.albumArtist !== (m.artist || v.artist) && m.albumArtist], ['Album', m.album || v.album],
		['Composer', m.composer], ['Year', m.year], ['Genre', m.genre], ['Track', ofN(m.track)], ['Disc', ofN(m.disc)],
		['Length', (m.duration || v.duration) ? fmtTime(m.duration || v.duration) : ''],
		['Format', formatName(m.container, p)],
		vid && ['Video', join(codecName(m.vcodec), res, m.fps ? +(+m.fps).toFixed(2) + ' fps' : '', kbps(m.videoBitrate))],
		['Audio', join(codecName(m.acodec), m.sampleRate ? m.sampleRate / 1000 + ' kHz' : '', m.channelLayout ? (m.channelLayout === 'stereo' || m.channelLayout === 'mono' ? m.channelLayout[0].toUpperCase() + m.channelLayout.slice(1) : m.channelLayout) : chName(m.channels), kbps(m.audioBitrate))],
		['Bitrate', kbps(m.bitrate)],
		(vid || (m.audio || []).length > 1) && ['Audio tracks', m.audio ? '' + m.audio.length : ''],
		vid && ['Subtitles', m.subs ? (m.subs.length ? '' + m.subs.length : 'None') : ''],
		v.chapters.length > 0 && ['Chapters', '' + v.chapters.length],
		['Size', row && row.size ? fmtSize(row.size) : m.size ? fmtSize(m.size) : ''],
		['Modified', row && row.mtime ? fmtDate(row.mtime) : ''],
	].filter(r => r && r[1]);
	return html`<section aria-label="Details"><dl class="details">${rows.map(([k, val]) => html`<dt>${k}</dt><dd>${val}</dd>`)}</dl>
		<div class="det-loc"><div class="det-k">Location</div><button class="det-path" onClick=${() => go(filesHash(dirName(p)))}><${Icon} n="folder" k=${2} />
			<span class="grow">${segs.map((sg, i) => html`${i > 0 && html`<span class="sep">›</span>`}<span>${i === 0 ? rootName(sg) : sg}</span>`)}<span class="sep">›</span><b>${baseName(p)}</b></span><span class="det-open">Show in Files</span></button></div></section>`;
}

function VolumeRow() {
	const s = store, here = s.target === 'browser', sn = s.snap;
	const [val, hold] = useHeld(here ? B.vol : sn ? sn.volume : 0);
	const send = useMemo(() => throttled(v => post('/api/volume', { v }).catch(() => {}), 120), []);
	const change = e => { const v = +e.target.value; hold(v); if (here) setBrowserVol(v); else send(v); };
	if (here && isIOS) return html`<div class="vol-note">Use your phone's volume buttons</div>`;
	// the server sends -1 when the handheld has no volume control to read (a desktop test run)
	if (!here && sn && sn.volume < 0) return html`<div class="vol-note">The handheld's volume can't be changed from here.</div>`;
	return html`<div class="vol-lab">${here ? 'Browser volume' : 'Handheld volume'}</div><div class="vol"><${Icon} n="speaker" k=${2} /><input type="range" min="0" max="100" value=${val} onInput=${change} aria-label=${here ? 'Browser volume' : 'Handheld volume'} aria-valuetext=${val + '%'} disabled=${!here && s.reconnecting} />
		<span class="pct">${val}%</span></div>`;
}

function ChaptersCard({ v }) {
	return html`<section class="card" id="chapters-card" aria-label="Chapters"><h2 style="margin-bottom:var(--s2)">Chapters</h2><div class="list-lines">
		${v.chapters.map((c, i) => html`<button class="line ${i === v.chapter ? 'cur' : ''}" aria-current=${i === v.chapter ? 'true' : undefined} onClick=${() => ctl.chapterTo(i)}>
			<span class="n">${i + 1}</span><span class="tt ell">${chapterTitle(c, i)}</span><span class="tm">${fmtTime(c.start)}</span></button>`)}</div></section>`;
}

// the queue, scrolled so the one playing is near the top
function UpNext({ v }) {
	const s = store, sn = s.snap;
	const [items, setItems] = useState(null), [idx, setIdx] = useState(0);
	const box = useRef();
	const key = v.here ? B.queue.length + ':' + B.index + ':' + (B.queue[0] && B.queue[0].p) : (sn && sn.path) + '|' + s.epoch + '|' + s.plTick;
	useEffect(() => {
		if (v.here) { setItems(B.queue.map(q => ({ p: q.p, title: titleFor(q.p, q.title) }))); setIdx(B.index); return; }
		let dead = false;
		get('/api/source').then(r => { if (!dead) { setItems((r.items || []).map(q => ({ p: q.p, title: titleFor(q.p, q.title) }))); setIdx(r.index); } }).catch(() => { if (!dead) setItems([]); });
		return () => { dead = true; };
	}, [key]);
	useLayoutEffect(() => {
		const b = box.current, c = b && b.querySelector('.cur'), sc = b && b.closest('.tabbody');
		if (c && sc) sc.scrollTop = Math.max(0, sc.scrollTop + c.getBoundingClientRect().top - sc.getBoundingClientRect().top - 8 - (c.offsetHeight || 0));
	}, [idx, items]);
	if (items === null) return html`<div class="skel" aria-hidden="true"></div>`;
	if (items.length < 2) return html`<p class="note pad">Nothing else is queued. Play a folder or a playlist to fill it.</p>`;
	const play = async i => {
		if (!userTap()) return;
		if (v.here) return browserPlay(B.queue, i, B.source, 0);
		const src = sn.source;
		try { if (src && src.kind === 'playlist' && src.id) await post('/api/play', { playlist: src.id, index: i }); else await post('/api/play', { p: items[i].p }); } catch (e) { toast(errMsg(e)); }
	};
	return html`<section class="card" aria-label="Queue"><div class="card-head"><h2 class="grow">Queue</h2><span class="dim tm">${idx + 1} of ${items.length}</span></div><div class="upnext list-lines" ref=${box}>
		${items.map((it, i) => html`<button class="line ${i === idx ? 'cur' : ''} ${i < idx ? 'past' : ''}" onClick=${() => play(i)} aria-current=${i === idx ? 'true' : undefined}>
			${i === idx ? html`<${Icon} n="play" k=${2} cls="y" />` : html`<span class="n">${i + 1}</span>`}<span class="tt ell">${i === idx && v.title ? v.title : it.title}</span></button>`)}</div></section>`;
}

/* ---- audio and subtitles ---- */
const SUB_COLORS = { white: '#FFFFFF', yellow: '#FFE45C', cyan: '#6EE7F2', green: '#8EF08A', pink: '#FF9BD2', grey: '#C8C8C8', black: '#000000', 'dark-grey': '#404040', 'dark-blue': '#10204A' };
const SUB_FONTS = { inter: ['Inter', 400], 'inter-bold': ['Inter', 700], pixelify: ['"Pixelify Sans"', 400], 'fusion-pixel': ['"Pixelify Sans"', 400] };	// (Fusion Pixel is 7 MB, for CJK: the preview's Latin looks the same in Pixelify)
const SUB_SIZES = { small: 36, medium: 46, large: 56, 'extra-large': 68 };
const SUB_BORDERS = { none: 0, thin: 1.5, normal: 3, thick: 5 };

function TracksCard({ v }) {
	const s = useStore(), tab = s.tracksTab || 'audio';
	const here = v.here;
	if (!here && !v.tracks) return null;	// a player too old to say
	const tabs = [['audio', 'Audio'], ['sub', 'Subtitles']];
	return html`<section class="card tracks" id="tracks-card" tabindex="-1" aria-label="Audio and subtitles">
		<div class="card-head"><h2 class="grow">Audio & Subtitles</h2>${here && html`<span class="chip">In this browser</span>`}</div>
		<div class="seg tabs2" role="tablist">${tabs.map(([k, l]) => html`<button role="tab" id=${'tt-' + k} aria-controls="tracks-panel" aria-selected=${tab === k} onClick=${() => set({ tracksTab: k })}>${l}</button>`)}</div>
		<div id="tracks-panel" role="tabpanel" aria-labelledby=${'tt-' + tab}>
		${here ? (tab === 'audio' ? html`<${BrowserAudio} />` : html`<${BrowserSubs} />`) : (tab === 'audio' ? html`<${HandheldAudio} t=${v.tracks} />` : html`<${HandheldSubs} t=${v.tracks} />`)}
		</div></section>`;
}
// the choice made here shows at once; the handheld's own word comes with the next state
function useChosen(serverId) {
	const [held, setHeld] = useState(null), t = useRef(0);
	useEffect(() => { setHeld(null); }, [serverId]);
	const hold = id => { setHeld(id); clearTimeout(t.current); t.current = setTimeout(() => setHeld(null), 4000); };
	return [held == null ? serverId : held, hold];
}
function TrackRow({ on, label, sub, onClick, disabled, tag }) {
	return html`<button class="radio track" role="radio" aria-checked=${!!on} disabled=${disabled} onClick=${onClick}><${Icon} n=${on ? 'radio-on' : 'radio'} k=${2} />
		<span class="grow"><span class="lab">${label}</span>${sub && html`<span class="sm">${sub}</span>`}</span>${tag && html`<span class="chip">${tag}</span>`}</button>`;
}
function HandheldAudio({ t }) {
	const list = t.audio || [];
	const [cur, hold] = useChosen(t.aid);
	if (!list.length) return html`<p class="note pad">This video has no sound.</p>`;
	if (list.length === 1) return html`<div class="radios"><${TrackRow} on=${true} label=${list[0].label} /></div><p class="note pad">Only one audio track</p>`;
	const pick = id => { if (id === cur) return; hold(id); post('/api/track', { kind: 'audio', id }).catch(e => { hold(null); errToast(e); }); };
	return html`<div class="radios" role="radiogroup" aria-label="Audio track">${list.map(a => html`<${TrackRow} on=${a.id === cur} label=${a.label} onClick=${() => pick(a.id)} />`)}</div>`;
}
function HandheldSubs({ t }) {
	const list = t.sub || [];
	const [cur, hold] = useChosen(t.sid || 0);
	const [styleOpen, setStyleOpen] = useState(false);
	const pick = id => { if (id === cur) return; hold(id); post('/api/track', { kind: 'sub', id }).catch(e => { hold(null); errToast(e); }); };
	return html`${list.length ? html`<div class="radios" role="radiogroup" aria-label="Subtitles">
			<${TrackRow} on=${!cur} label="Off" onClick=${() => pick(0)} />
			${list.map(x => html`<${TrackRow} on=${x.id === cur} label=${x.label} sub=${x.forced ? 'Forced' : x.external ? 'From a file beside the video' : ''} onClick=${() => pick(x.id)} />`)}</div>`
		: html`<p class="note pad">No subtitles in this file</p>`}
		<button class="line more-row" aria-expanded=${styleOpen} onClick=${() => setStyleOpen(!styleOpen)}><${Icon} n="pencil" k=${2} /><span class="grow">Subtitle style</span><${Icon} n=${styleOpen ? 'up' : 'down'} k=${2} /></button>
		${styleOpen && html`<${SubStyle} />`}`;
}
function SubStyle() {
	const [st, setSt] = useState(null), [err, setErr] = useState('');
	useEffect(() => { get('/api/substyle').then(setSt).catch(e => setErr(e.status === 404 ? 'Change it on the handheld: START › Settings › Subtitles.' : errMsg(e))); }, [store.epoch]);
	if (err) return html`<p class="note pad">${err}</p>`;
	if (!st) return html`<div class="skel" style="height:120px" aria-hidden="true"></div>`;
	const val = k => { const x = (st.settings || []).find(y => y.key === k); return x && x.value; };
	const change = (k, value) => {
		setSt({ ...st, settings: st.settings.map(x => x.key === k ? { ...x, value } : x) });
		post('/api/substyle', { key: k, value }).then(r => r && r.settings && setSt(r)).catch(errToast);
	};
	const f = SUB_FONTS[val('font')] || SUB_FONTS.inter, size = (SUB_SIZES[val('size')] || 46) * .42, bw = (SUB_BORDERS[val('border')] || 0) * .42;
	const prev = `font-family:${f[0]};font-weight:${f[1]};font-size:${size.toFixed(1)}px;color:${SUB_COLORS[val('color')] || '#fff'};-webkit-text-stroke:${(bw * 2).toFixed(2)}px ${SUB_COLORS[val('border-color')] || '#000'}`;
	return html`<div class="substyle">
		${st.styled && html`<div class="note warn">This video's subtitles are styled (ASS): they keep their own look.</div>`}
		<div class="sub-preview" aria-hidden="true"><span style=${prev}>Subtitles look like this</span></div>
		${(st.settings || []).map(x => html`<div class="ss-row"><div class="ss-lab" id=${'ss-' + x.key}>${x.label}</div>
			${x.key === 'font' ? html`<select class="sel" aria-labelledby=${'ss-' + x.key} value=${x.value} onChange=${e => change(x.key, e.target.value)}>${x.choices.map(c => html`<option value=${c.id}>${c.label}</option>`)}</select>`
			: /color/.test(x.key) ? html`<div class="swatches" role="radiogroup" aria-labelledby=${'ss-' + x.key}>${x.choices.map(c => html`<button class="sw" role="radio" aria-checked=${c.id === x.value} onClick=${() => change(x.key, c.id)}><i style="background:${SUB_COLORS[c.id] || '#888'}"></i><span>${c.label}</span></button>`)}</div>`
			: html`<div class="chips" role="radiogroup" aria-labelledby=${'ss-' + x.key}>${x.choices.map(c => html`<button class="chipbtn" role="radio" aria-checked=${c.id === x.value} onClick=${() => change(x.key, c.id)}>${c.label}</button>`)}</div>`}</div>`)}
		<p class="note">How plain subtitles (SRT, VTT…) look. Styled ones (ASS) keep theirs.</p></div>`;
}
function BrowserAudio() {
	const m = B.meta && B.meta.media, list = m && m.audio || [];
	const at = el.audioTracks, can = at && at.length > 1;
	const playing = can ? [...at].findIndex(t => t.enabled) : Math.max(0, list.findIndex(a => a.default));
	const [want, setWant] = useState(-1);
	if (!list.length) return html`<p class="note pad">${m ? 'This video has no sound.' : 'Loading…'}</p>`;
	if (list.length === 1) return html`<div class="radios"><${TrackRow} on=${true} label=${list[0].label} tag="Playing here" /></div><p class="note pad">Only one audio track</p>`;
	const pick = i => { if (can) { [...at].forEach((t, j) => t.enabled = j === i); notify(); } else setWant(i === playing ? -1 : i); };
	return html`<div class="radios" role="radiogroup" aria-label="Audio track">${list.map((a, i) => html`<${TrackRow} on=${want >= 0 ? i === want : i === playing} label=${a.label} tag=${i === playing ? 'Playing here' : ''} onClick=${() => pick(i)} />`)}</div>
		${!can && html`<div class="track-note"><p class="note">This browser plays the first audio track only.</p>
			<button class="btn sm ${want >= 0 ? 'primary' : ''}" onClick=${() => handoffToHandheld(want >= 0 ? list[want].i + 1 : undefined)}><${Icon} n="card" k=${2} />${want >= 0 ? 'Play this one on the handheld' : 'Change it on the handheld'}</button></div>`}`;
}
function BrowserSubs() {
	useStore();
	const list = SUBS.list, cur = SUBS.chosen;
	const text = list.filter(x => x.kind !== 'image'), pics = list.filter(x => x.kind === 'image');
	const pick = i => { lsSet('bpSubOff', i < 0 ? '1' : '0'); chooseSub(i); };
	const failed = Object.values(SUBS.failed)[0];
	return html`${list.length ? html`<div class="radios" role="radiogroup" aria-label="Subtitles">
			<${TrackRow} on=${cur < 0} label="Off" onClick=${() => pick(-1)} />
			${text.map(x => html`<${TrackRow} on=${x.i === cur} label=${x.label} sub=${x.i === cur && SUBS.progress != null ? 'Preparing subtitles… ' + Math.round(SUBS.progress * 100) + '%' : x.external ? 'From a file beside the video' : ''} onClick=${() => pick(x.i)} />`)}
			${pics.map(x => html`<${TrackRow} on=${false} disabled=${true} label=${x.label} sub="Picture subtitles show on the handheld only" />`)}</div>`
		: html`<p class="note pad">${B.meta && B.meta.media ? 'No subtitles in this file' : 'Loading…'}</p>`}
		${failed && html`<p class="note pad err" role="alert">${failed}</p>`}
		${pics.length > 0 && html`<div class="track-note"><button class="btn sm" onClick=${() => handoffToHandheld()}><${Icon} n="card" k=${2} />Watch on handheld</button></div>`}
		<div class="substyle here">
			<div class="ss-row"><div class="ss-lab" id="cue-size">Size</div><div class="chips" role="radiogroup" aria-labelledby="cue-size">${CUE_SIZES.map(([k, l]) => html`<button class="chipbtn" role="radio" aria-checked=${cueStyle.size === k} onClick=${() => setCueStyle('size', k)}>${l}</button>`)}</div></div>
			<div class="ss-row"><div class="ss-lab" id="cue-pos">Position</div><div class="chips" role="radiogroup" aria-labelledby="cue-pos">${[['bottom', 'Bottom'], ['raised', 'Raised']].map(([k, l]) => html`<button class="chipbtn" role="radio" aria-checked=${cueStyle.pos === k} onClick=${() => setCueStyle('pos', k)}>${l}</button>`)}</div></div>
			<p class="note">How subtitles look in this browser, on this device.</p></div>`;
}

/* ---- sound: equalizer and effects ---- */
const BANDS = [31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000];
const bandLabel = f => f >= 1000 ? f / 1000 + 'k' : '' + f;
const FX = [['none', 'None'], ['room', 'Room'], ['hall', 'Concert hall'], ['church', 'Church'], ['cave', 'Cave'], ['wide', 'Wide stereo'], ['headphones', 'Headphones']];
const dbText = g => (g > 0 ? '+' : g < 0 ? '−' : '') + Math.abs(g).toFixed(1);
// a song in this browser: the equalizer and effects are the handheld's
function SoundHere() {
	return html`<section class="card sound-here" aria-label="Sound"><span class="grow note">Equalizer and effects play on the handheld</span><button class="btn sm" onClick=${() => setTarget('handheld')}><${Icon} n="card" k=${2} />Switch to handheld</button></section>`;
}
function SoundCard({ v }) {
	const sn = store.snap, eq = sn && sn.eq || { on: false, preset: 'Flat', gains: Array(10).fill(0) };
	const [tab, setTab] = useState('eq');
	const [gains, holdGains] = useHeld(eq.gains);
	const [selBand, setSelBand] = useState(2);
	const wide = useMedia('(min-width: 768px)');
	const sendGains = useMemo(() => throttled(g => post('/api/eq', { gains: g.join(',') }).catch(e => errToast(e)), 120), []);
	const setGain = (i, val) => { const g = gains.slice(); g[i] = Math.max(-12, Math.min(12, val)); holdGains(g); sendGains(g); };
	const here = v.here, vid = v.kind === 'video', off = here || vid;
	const presets = store.eq || [{ name: 'Flat' }];
	const cd = eq.preset === '';
	return html`<section class="card sound" aria-label="Sound" aria-disabled=${off ? 'true' : undefined}>
		<div class="head"><h2 class="grow">Sound</h2></div>
		<div class="seg tabs2" role="tablist">${[['eq', 'Equalizer'], ['fx', 'Effects']].map(([k, l]) => html`<button role="tab" aria-selected=${tab === k} onClick=${() => setTab(k)}>${l}</button>`)}</div>
		${off && html`<div class="note" style="margin-bottom:var(--s3)">${here ? 'Equalizer and effects play on the handheld only.' : 'Sound changes music only.'}</div>`}
		<div class=${off ? 'greyed' : ''} inert=${off ? true : undefined}>
		${tab === 'eq' ? html`
			<div class="sound-ctl">
				<button class="switch" role="switch" aria-checked=${eq.on} onClick=${() => post('/api/eq', { on: eq.on ? 0 : 1 }).catch(e => errToast(e))}><span class="knob"></span>${eq.on ? 'On' : 'Off'}</button>
				<select class="sel grow" aria-label="Preset" value=${eq.preset} onChange=${e => post('/api/eq', { preset: e.target.value }).catch(x => errToast(x))}>
					${cd && html`<option value="">Custom</option>`}${presets.map(p => html`<option value=${p.name}>${p.name}</option>`)}</select>
				<button class="btn sm" onClick=${() => { holdGains(Array(10).fill(0)); post('/api/eq', { preset: 'Flat' }).catch(() => {}); }}>Reset</button>
			</div>
			<div class=${eq.on ? '' : 'greyed'}>${wide ? html`<div class="eq-v">${BANDS.map((f, i) => html`<div class="eq-band"><div class="val ${gains[i] ? 'on' : ''}">${gains[i] ? dbText(gains[i]).replace('.0', '') : ''}</div>
				<div class="rail"><input type="range" min="-12" max="12" step="0.5" value=${gains[i]} aria-label=${bandLabel(f) + ' Hz'} aria-valuetext=${dbText(gains[i]) + ' dB'} onInput=${e => setGain(i, +e.target.value)} onDblClick=${() => setGain(i, 0)} /></div>
				<div class="hz">${bandLabel(f)}</div></div>`)}</div>` : html`
				<div class="eq-bars" role="group" aria-label="Bands">${BANDS.map((f, i) => { const g = gains[i], h = Math.abs(g) / 12 * 50; return html`<button class="${g >= 0 ? 'pos' : 'neg'} ${i === selBand ? 'sel' : ''} ${g ? 'nz' : ''}" aria-label=${bandLabel(f) + ' Hz, ' + dbText(g) + ' dB'} aria-pressed=${i === selBand} onClick=${() => setSelBand(i)}><i style="height:${h}%"></i><span class="hz">${bandLabel(f)}</span></button>`; })}</div>
				<div class="eq-edit"><button class="ib" aria-label="Lower" onClick=${() => setGain(selBand, gains[selBand] - 0.5)}><${Icon} n="minus" k=${2} /></button>
					<input type="range" min="-12" max="12" step="0.5" value=${gains[selBand]} aria-label=${BANDS[selBand] + ' Hz'} aria-valuetext=${dbText(gains[selBand]) + ' dB'} onInput=${e => setGain(selBand, +e.target.value)} />
					<button class="ib" aria-label="Raise" onClick=${() => setGain(selBand, gains[selBand] + 0.5)}><${Icon} n="plus" k=${2} /></button></div>
				<div class="eq-edit" style="margin-top:4px;justify-content:center"><span class="lab">${BANDS[selBand] >= 1000 ? BANDS[selBand] / 1000 + ' kHz' : BANDS[selBand] + ' Hz'} · ${dbText(gains[selBand])} dB</span></div>`}</div>` : html`
			<div class="fx-list" role="radiogroup" aria-label="Effect">${FX.map(([k, l]) => html`<button class="radio" role="radio" aria-checked=${(sn && sn.fx || 'none') === k} onClick=${() => post('/api/fx', { key: k }).catch(e => errToast(e))}><${Icon} n=${(sn && sn.fx || 'none') === k ? 'radio-on' : 'radio'} k=${2} />${l}</button>`)}</div>`}
		</div></section>`;
}

/* ================= playlist edits shared by Files and Playlists ================= */
async function plEdit(id, act, params, rev) {
	if (rev == null) rev = (await get('/api/playlist', { id })).rev;
	return post('/api/playlist/' + act, Object.assign({ id, rev }, params));
}
const validPlName = (n, others, self) => {
	n = n.trim();
	if (!n) return 'Give it a name';
	if (n.length > 40) return 'Names can be 40 characters at most';
	if (n.includes('/')) return "Names can't contain /";
	if (others.some(o => o.id !== self && o.name.toLowerCase() === n.toLowerCase())) return 'A playlist with that name already exists';
	return '';
};

/* a one-field dialog; submit returns an error text or '' when done */
function NameDialog({ title, sub, label, initial, ok, submit, onClose, select }) {
	const [val, setVal] = useState(initial || ''), [err, setErr] = useState(''), [busy, setBusy] = useState(false), ref = useRef();
	useEffect(() => { const i = ref.current; i.focus(); if (select) { const dot = initial && initial.lastIndexOf('.'); i.setSelectionRange(0, dot > 0 ? dot : (initial || '').length); } }, []);
	const go = async e => { e && e.preventDefault(); if (busy) return; setBusy(true); const m = await submit(ref.current ? ref.current.value : val); setBusy(false); if (m) setErr(m); else onClose(); };
	return html`<${Modal} title=${title} sub=${sub} onClose=${onClose} actions=${html`<button class="btn" onClick=${onClose}>Cancel</button><button class="btn primary" disabled=${busy} onClick=${go}>${ok}</button>`}>
		<form onSubmit=${go}><input ref=${ref} class="field ${err ? 'bad' : ''}" value=${val} aria-label=${label} aria-invalid=${err ? 'true' : undefined} onInput=${e => { setVal(e.target.value); setErr(''); }} autocomplete="off" />
		<div class="errtxt" role="alert">${err}</div></form></${Modal}>`;
}
const nameDialog = props => openModal(close => html`<${NameDialog} ...${props} onClose=${close} />`);

function newPlaylistDialog(onCreated) {
	let list = [];
	get('/api/playlists').then(r => list = r.playlists || []).catch(() => {});
	nameDialog({ title: 'New playlist', label: 'Playlist name', ok: 'Create', submit: async n => {
		const m = validPlName(n, list); if (m) return m;
		try { const r = await post('/api/playlists/create', { name: n.trim() }); set({ plTick: store.plTick + 1 }); onCreated && onCreated(r.id, n.trim()); return ''; } catch (e) { return e.message; }
	} });
}

/* ================= Like ================= */
// the liked files' virtual paths (a network file isn't in the web's tree: the playing one's flag comes in the state)
store.likes = null; store.likesCount = 0; store.likeHeld = {};
const LIKED_ID = '.liked', LIKED_MAX = 2000;
let likesSeq = 0;
async function loadLikes() {
	const n = ++likesSeq;
	try { const r = await get('/api/likes', null, { quiet: true }); if (n === likesSeq) set({ likes: new Set(r.paths || []), likesCount: r.count || 0 }); } catch (e) {}
}
// what a heart shows: a press on its way first, then the state's flag for the handheld's file, then the list
function isLiked(p) {
	if (!p) return false;
	const h = store.likeHeld[p];
	if (h && Date.now() < h.until) return h.v;
	const s = store.snap;
	if (s && s.path === p && typeof s.liked === 'boolean') return s.liked;
	return !!(store.likes && store.likes.has(p));
}
const likable = v => !!(v && v.has && v.path && (v.kind === 'audio' || v.kind === 'video'));
// key: the file's virtual path (what the hearts look up); p: what the server is asked about, the same path, or
// "current" for the handheld's file. how 'menu': a menu's Like says so, with Undo (a heart is its own answer)
async function setLike(key, on, how, p) {
	if (isLiked(key) === on) return true;	// liking twice changes nothing
	store.likeHeld[key] = { v: on, until: Infinity }; notify();
	try {
		const r = await post('/api/like', { p: p || key, on: on ? 1 : 0 });
		const likes = new Set(store.likes || []);
		if (r.path) { if (r.liked) likes.add(r.path); else likes.delete(r.path); }
		// held a moment more: the state's flag comes after the answer
		store.likeHeld[key] = { v: !!r.liked, until: Date.now() + 1500 };
		// a full Liked (2,000) makes room: the oldest like goes, and the server says which
		if (r.full && r.dropped) likes.delete(r.dropped);
		set({ likes, likesCount: Math.min(LIKED_MAX, Math.max(0, store.likesCount + (on ? 1 : -1))) });
		if (r.full) toast('Liked is full: the oldest one went');
		else if (how === 'menu') toast(on ? 'Added to Liked' : 'Removed from Liked', { label: 'Undo', fn: () => setLike(key, !on, '', p) });
		return true;
	} catch (e) {
		delete store.likeHeld[key]; notify();
		if (e.status === 409 && e.body.code === 'idle') toast('Nothing is playing');
		else if (e.status >= 500 && e.status !== 503) toast("Couldn't save Liked", { label: 'Try again', fn: () => setLike(key, on, how, p) });
		else errToast(e);
		return false;
	}
}
// the heart for what plays: the browser's own file when it plays here, else the handheld's
function LikeBtn({ v, cls }) {
	const on = isLiked(v.path), [pop, setPop] = useState(false), seen = useRef({ p: v.path, on }), t = useRef(0);
	useEffect(() => {
		const was = seen.current; seen.current = { p: v.path, on };
		if (on && !was.on && was.p === v.path && !reduceMotion()) { setPop(true); clearTimeout(t.current); t.current = setTimeout(() => setPop(false), 320); }
	}, [on, v.path]);
	return html`<button class=${cls + ' like' + (on ? ' on' : '') + (pop ? ' pop' : '')} aria-pressed=${on} aria-label=${on ? 'Unlike' : 'Like'} title=${on ? 'Liked · click to unlike' : 'Like'}
		onClick=${e => { e.stopPropagation(); setLike(v.path, !on, '', v.here ? v.path : 'current'); }}><${Icon} n=${on ? 'heart' : 'heart-o'} k=${2} /></button>`;
}
function showInFiles(p) { go(filesHash(dirName(p))); setTimeout(() => markFresh(p), 300); }

/* ================= Add to playlist ================= */
// the picker's order: the playlists added to last first (as this browser remembers), then A–Z
function plUsed() { try { return JSON.parse(lsGet('bpPlUsed') || '{}') || {}; } catch (e) { return {}; } }
function markUsed(id) { const u = plUsed(); u[id] = Date.now(); lsSet('bpPlUsed', JSON.stringify(u)); }
// paths: what's added (files, or a folder's media); what: its name, for the panel and the toast. anchor: the bar's
// button on a computer, which the panel pops over (a sheet otherwise)
function addToPlaylist(paths, what, anchor) {
	if (anchor && !compactNow()) {
		const r = anchor.getBoundingClientRect();
		const pos = { left: Math.max(8, Math.min(innerWidth - 388, r.left - 160)), bottom: innerHeight - r.top + 10 };
		set({ addOpen: true });
		openModal(close => html`<${AddTo} paths=${paths} what=${what} pos=${pos} onClose=${() => { set({ addOpen: false }); close(); }} />`, () => set({ addOpen: false }));
	} else openModal(close => html`<${AddTo} paths=${paths} what=${what} onClose=${close} />`);
}
function addToPlaylistDialog(paths) { addToPlaylist(paths, paths.length === 1 ? titleFor(paths[0]) : plural(paths.length, 'item', 'items')); }
function AddTo({ paths, what, pos, onClose }) {
	const s = useStore();
	// one song or video: which playlists have it already (✓ Added)
	const one = paths.length === 1 && ['audio', 'video'].includes(kindOfName(paths[0])) ? paths[0] : null;
	const [list, setList] = useState(null), [has, setHas] = useState({}), [find, setFind] = useState(''), [naming, setNaming] = useState(null), [err, setErr] = useState(''), [armed, setArmed] = useState(null), [busy, setBusy] = useState(false);
	const ref = useRef(), nameRef = useRef(), findRef = useRef();
	// (again when a playlist changes anywhere: the marks follow)
	useEffect(() => {
		let gone = false;
		get('/api/playlists').then(async r => {
			const all = r.playlists || []; if (gone) return;
			setList(all);
			if (!one) return;
			const h = {};
			await Promise.all(all.filter(p => !p.builtin && p.count).map(p => get('/api/playlist', { id: p.id }, { quiet: true }).then(x => { if ((x.items || []).some(i => i.p === one)) h[p.id] = 1; }).catch(() => {})));
			if (!gone) setHas(h);
		}).catch(e => { if (!gone && !list) { toast(errMsg(e)); onClose(); } });
		return () => { gone = true; };
	}, [s.plTick, s.epoch]);
	const used = plUsed();
	const pls = (list || []).filter(p => !p.builtin).sort((a, b) => ((used[b.id] || 0) - (used[a.id] || 0)) || cmpName(a.name, b.name));
	const many = pls.length > 7, f = find.trim().toLowerCase();
	const shown = f ? pls.filter(p => p.name.toLowerCase().includes(f)) : pls;
	const exact = f && pls.some(p => p.name.toLowerCase() === f);
	const loaded = list !== null;
	// a computer: straight into Find; the popover's first row otherwise (keys work at once)
	useEffect(() => {
		if (!loaded) return;
		if (many && !touchOnly && findRef.current) findRef.current.focus();
		else if (pos && ref.current) { const b = ref.current.querySelector('.sheet-item'); if (b) b.focus(); }
	}, [loaded]);
	useEffect(() => { if (naming != null && nameRef.current) nameRef.current.focus(); }, [naming != null]);
	useEffect(() => {
		if (!pos) return;
		const k = e => { if (e.key === 'Escape') { e.preventDefault(); onClose(); } };
		document.addEventListener('keydown', k); return () => document.removeEventListener('keydown', k);
	}, []);
	const open = id => ({ label: 'Open', fn: () => go('#/playlists/' + encodeURIComponent(id)) });
	const add = async p => {
		if (busy) return;
		const again = !!has[p.id];
		// already in it: the first press asks, the second adds a second copy
		if (again && armed !== p.id) { setArmed(p.id); return; }
		setBusy(true);
		try { await plEdit(p.id, 'add', { p: paths }); markUsed(p.id); onClose(); toast('Added to ' + p.name + (again ? ' again' : ''), open(p.id)); }
		catch (e) { setBusy(false); toast(errMsg(e)); }
	};
	const create = async name => {
		const m = validPlName(name, list || []);
		if (m) { setNaming(name); setErr(m); return; }
		setBusy(true);
		try {
			const r = await post('/api/playlists/create', { name: name.trim() });
			await plEdit(r.id, 'add', { p: paths }); markUsed(r.id); onClose();
			toast('Made ' + name.trim() + ' and added ' + what, open(r.id));
		} catch (e) { setBusy(false); setNaming(name); setErr(errMsg(e)); }
	};
	// Enter adds to the first match, or makes a playlist of that name when nothing matches
	const findKey = e => { if (e.key !== 'Enter') return; e.preventDefault(); if (shown.length) add(shown[0]); else if (f) create(find); };
	// Esc leaves the name field; a second one closes
	const nameKey = e => { if (e.key === 'Escape') { e.preventDefault(); e.stopPropagation(); setNaming(null); setErr(''); } };
	const hl = n => { if (!f) return n; const i = n.toLowerCase().indexOf(f); return i < 0 ? n : html`${n.slice(0, i)}<span class="hl">${n.slice(i, i + f.length)}</span>${n.slice(i + f.length)}`; };
	const body = !loaded ? html`<div class="none">Loading…</div>` : html`
		${many && html`<div class="find"><${Icon} n="search" k=${2} /><input class="field" ref=${findRef} value=${find} placeholder="Find a playlist" aria-label="Find a playlist" autocomplete="off" enterkeyhint="go" onInput=${e => { setFind(e.target.value); setArmed(null); }} onKeyDown=${findKey} /></div>`}
		<div class="picklist">
			${naming != null ? html`<form class="newpl" onSubmit=${e => { e.preventDefault(); create(nameRef.current ? nameRef.current.value : naming); }}>
				<input class=${'field' + (err ? ' bad' : '')} ref=${nameRef} value=${naming} placeholder="Playlist name" aria-label="New playlist's name" aria-invalid=${err ? 'true' : undefined} autocomplete="off" enterkeyhint="done" onInput=${e => { setNaming(e.target.value); setErr(''); }} onKeyDown=${nameKey} />
				<button class="btn primary" disabled=${busy}>Create</button></form>${err && html`<div class="errtxt" role="alert">${err}</div>`}`
			: html`<button class="sheet-item" onClick=${() => { setNaming(find.trim()); setErr(''); }}><${Icon} n="plus" k=${2} /><span class="grow">${f && !exact ? 'New playlist “' + find.trim() + '”' : 'New playlist…'}</span></button>`}
			${shown.map(p => { const arm = armed === p.id; return html`<button class=${'sheet-item' + (arm ? ' armed' : '')} key=${p.id} disabled=${busy} onClick=${() => add(p)}>
				<${Icon} n="playlist" k=${2} /><span class="grow">${hl(p.name)}<span class="sm">${arm ? 'Already in it' : plural(p.count, 'item', 'items')}</span></span>
				${arm ? html`<span class="r">Add again</span>` : has[p.id] ? html`<span class="r"><${Icon} n="check" k=${2} />Added</span>` : null}</button>`; })}
			${f && !shown.length && html`<div class="none">No playlist called “${find.trim()}”</div>`}</div>`;
	if (pos) return html`<div class="popcatch" onMouseDown=${onClose}></div>
		<div class="addpop addto" role="dialog" aria-label=${'Add ' + what + ' to a playlist'} ref=${ref} style=${`left:${pos.left}px;bottom:${pos.bottom}px`}>
			<h2>Add to playlist</h2><div class="sub ell">${what}</div><div class="body">${body}</div></div>`;
	return html`<${Modal} cls="addto" title="Add to playlist" sub=${what} onClose=${onClose} actions=${html`<button class="btn" onClick=${onClose}>Cancel</button>`}>${body}</${Modal}>`;
}

/* ================= files ================= */
// one rule for a file or folder name, wherever it's typed (FAT cards take none of these)
const nameError = n => !n ? 'Give it a name' : /[\/\\:*?"<>|]/.test(n) ? 'Names can\'t use / \\ : * ? " < > |' : n === '.' || n === '..' ? 'That name is taken by the card' : '';
store.knownFolders = new Set();
const GLYPH = { folder: 'folder', audio: 'note', video: 'film', photo: 'picture', sub: 'cc', other: 'info' };
const rootName = seg => (store.roots || []).find(r => r.path === seg)?.name || 'This card';

function MovePicker({ paths, start, onClose, done }) {
	const [dir, setDir] = useState(start), [items, setItems] = useState(null), [err, setErr] = useState(''), [busy, setBusy] = useState(false), [tick, setTick] = useState(0);
	useEffect(() => { setItems(null); get('/api/ls', { p: dir }).then(r => setItems(r.items.filter(i => i.dir).map(i => i.name).filter(n => !paths.includes(joinPath(dir, n))).sort(cmpName))).catch(e => setErr(errMsg(e))); }, [dir, tick]);
	const mkdir = () => nameDialog({ title: 'New folder', label: 'Folder name', ok: 'Create', submit: async n => { n = n.trim(); const bad = nameError(n); if (bad) return bad; try { await post('/api/mkdir', { p: dir, name: n }); setDir(joinPath(dir, n)); setTick(t => t + 1); return ''; } catch (e) { return e.message; } } });
	const segs = dir.split('/');
	const move = async () => { setBusy(true); try { await post('/api/move', { p: paths, to: dir }); onClose(); done(); toast('Moved ' + plural(paths.length, 'item', 'items') + ' to ' + (segs.length > 1 ? segs[segs.length - 1] : rootName(segs[0]))); } catch (e) { setErr(errMsg(e)); setBusy(false); } };
	return html`<${Modal} title=${'Move ' + plural(paths.length, 'item', 'items') + ' to…'} onClose=${onClose} actions=${html`<button class="btn" onClick=${onClose}>Cancel</button><button class="btn primary" disabled=${busy || dir === dirName(paths[0])} onClick=${move}>Move here</button>`}>
		<div class="crumbs"><nav aria-label="Destination">${segs.map((sg, i) => html`${i > 0 && html`<span class="sep">›</span>`}<button onClick=${() => setDir(segs.slice(0, i + 1).join('/'))}>${i === 0 ? rootName(sg) : sg}</button>`)}</nav><button class="btn sm ghost" onClick=${mkdir}><${Icon} n="plus" k=${2} />New folder</button></div>
		<div class="picklist">${items === null ? html`<div style="padding:var(--s4)" class="dim">Loading…</div>` : items.length === 0 ? html`<div style="padding:var(--s4)" class="dim">No folders here</div>` :
			items.map(n => html`<button class="sheet-item" onClick=${() => setDir(joinPath(dir, n))}><${Icon} n="folder" k=${2} cls="gl-y" />${n}</button>`)}</div>
		<div class="errtxt" role="alert">${err}</div></${Modal}>`;
}

function downloadPaths(paths) {
	const files = paths.filter(p => !store.knownFolders.has(p));
	if (files.length < paths.length) toast('Folders can’t be downloaded: ' + (files.length ? 'downloading the files only' : 'pick files'));
	files.forEach((p, i) => setTimeout(() => { const a = document.createElement('a'); a.href = '/api/file?p=' + encodeURIComponent(p) + '&dl=1'; a.download = baseName(p); document.body.appendChild(a); a.click(); a.remove(); }, i * 500));
}
// info: {name -> listing row} for the counts in a folder's question
async function deletePaths(paths, done, info) {
	const one = paths.length === 1, row = one && info && info[baseName(paths[0])];
	const sn = store.snap, hp = sn && sn.path;
	const playing = !!hp && paths.some(p => hp === p || hp.startsWith(p + '/'));
	let title;
	if (one && row && row.dir) title = row.items || row.itemsMore ? `Delete “${row.name}” and ${row.itemsMore ? 'everything' : row.items === 1 ? 'the 1 item' : 'the ' + row.items.toLocaleString() + ' items'} in it?` : `Delete “${row.name}”?`;
	else title = one ? `Delete “${baseName(paths[0])}”?` : 'Delete ' + plural(paths.length, 'item', 'items') + '?';
	const sub = (playing ? (one ? "It's playing on your handheld. " : 'One of them is playing on your handheld. ') : '') + (one ? "It's removed from the card. This can't be undone." : "They're removed from the card. This can't be undone.");
	const r = await ask(title, sub, { buttons: [{ label: 'Cancel', value: null }, { label: playing ? 'Stop and delete' : 'Delete', value: true, cls: 'danger' }] });
	if (!r) return;
	const del = stop => post('/api/delete', { p: paths, stop: stop ? 1 : undefined });
	try { await del(playing); toast('Deleted ' + plural(paths.length, 'item', 'items')); done(); }
	catch (e) {
		if (e.status === 409 && e.body.code === 'playing' && !playing) {
			if (await ask("It's playing on your handheld", 'Stop it there and delete it?', { buttons: [{ label: 'Cancel', value: null }, { label: 'Stop and delete', value: true, cls: 'danger' }] })) {
				try { await del(true); toast('Deleted ' + plural(paths.length, 'item', 'items')); } catch (x) { toast(x.message); }
			}
		} else errToast(e.status === 409 ? Object.assign(e, { message: 'Can’t delete: ' + e.message }) : e);
		done();
	}
}
async function setHidden(paths, on, done) {
	let n = 0;
	for (const p of paths) { try { await post('/api/hide', { p, on: on ? 1 : 0 }); n++; } catch (e) { errToast(e); break; } }
	done && done();
	if (!n) return;
	const what = n === 1 ? '“' + baseName(paths[0]) + '”' : plural(n, 'folder', 'folders');
	toast(what + (n === 1 ? ' is' : ' are') + (on ? ' hidden' : ' shown again'), { label: 'Undo', fn: () => setHidden(paths.slice(0, n), !on, done) });
}
function setShowHidden(on) {
	if (store.snap) { store.snap.showHiddenFolders = on; notify(); }
	post('/api/setting', { key: 'showHiddenFolders', value: on ? 1 : 0 }).catch(errToast);
}
function renamePath(p, done) {
	const old = baseName(p), isDir = store.knownFolders.has(p);
	nameDialog({ title: 'Rename', label: 'New name', initial: old, ok: 'Rename', select: !isDir, submit: async n => {
		n = n.trim(); const bad = nameError(n); if (bad) return bad;
		if (n === old) return '';
		try { await post('/api/rename', { p, name: n }); done(); return ''; } catch (e) { return e.message; }
	} });
}

function Files() {
	const s = useStore(), route = s.route, path = route.path;
	const roots = s.roots;
	const [data, setData] = useState(null), [err, setErr] = useState(''), [selMode, setSelMode] = useState(false), [over, setOver] = useState(false);
	const fileIn = useRef(), dirIn = useRef(), lp = useRef({});
	const sel = s.sel, pick = s.pick;
	useEffect(() => { if (!path && roots && roots.length === 1) replaceHash(filesHash(roots[0].path)); }, [path, roots]);
	const load = useCallback(async quiet => {
		if (!path) return;
		if (!quiet) setData(null);
		try { const r = await get('/api/ls', { p: path }); r.items.forEach(i => { if (i.dir) store.knownFolders.add(joinPath(path, i.name)); }); setData(r); setErr(''); }
		catch (e) { if (e.status === 404 && path.includes('/')) replaceHash(filesHash(dirName(path))); else setErr(errMsg(e)); }
	}, [path]);
	useEffect(() => { load(false); store.lastDir = path; if (!store.pick) set({ sel: new Set() }); setSelMode(false); }, [path]);
	// back from a lost connection: the same folder, quietly up to date (the selection stays)
	useEffect(() => { if (s.epoch && (data || err)) load(!!data); }, [s.epoch]);
	useEffect(() => { if (data && (!s.fsDir || s.fsDir === path)) load(true); }, [s.fsTick]);
	const slow = useLater(!data && !err, 300);
	const items = useMemo(() => data ? data.items.slice().sort((a, b) => (b.dir - a.dir) || cmpName(a.name, b.name)) : [], [data]);
	const full = n => joinPath(path, n);
	const refresh = () => { set({ sel: new Set() }); setSelMode(false); load(true); };
	const setSel = x => set({ sel: new Set(x) });
	const toggle = p => { const n = new Set(store.sel); n.has(p) ? n.delete(p) : n.add(p); setSel(n); };
	const selected = [...sel], selCount = selected.length;
	const showSel = selMode || selCount > 0 || !!pick;
	const here = s.target === 'browser';
	// the phone's small player makes room for the selection's bar
	useEffect(() => { set({ selecting: showSel && !pick }); return () => set({ selecting: false }); }, [showSel, !!pick]);
	const byName = useMemo(() => { const o = {}; (data ? data.items : []).forEach(i => o[i.name] = i); return o; }, [data]);
	// "Show hidden folders" changed here or on the handheld: the list follows
	const showHidden = s.snap ? s.snap.showHiddenFolders : undefined, canHide = showHidden !== undefined;
	const seenHidden = useRef(showHidden);
	useEffect(() => { if (seenHidden.current !== showHidden) { seenHidden.current = showHidden; if (data) load(true); } }, [showHidden]);
	const selFolders = [...sel].filter(p => dirName(p) === path && byName[baseName(p)] && byName[baseName(p)].dir);
	const selAllHidden = selFolders.length > 0 && selFolders.every(p => byName[baseName(p)].hidden);

	function rowSheet(it) {
		const p = full(it.name), isDir = it.dir, k = it.kind;
		const playable = k === 'audio' || k === 'video';
		const playH = { label: isDir ? 'Play folder on handheld' : 'Play on handheld', icon: 'card', fn: () => playOn('handheld', [p]) };
		const playB = { label: isDir ? 'Play folder here' : 'Play in this browser', icon: 'network', disabled: NO_HERE || it.playable === 'no', sub: NO_HERE ? NO_HERE_WHY : it.playable === 'no' ? 'Plays on the handheld only' : undefined, fn: () => playOn('browser', [p]) };
		const plays = (isDir || playable) ? (here ? [playB, playH] : [playH, playB]) : [];
		openSheet(it.name, isDir ? 'folder' : GLYPH[k], [...plays,
			// a song or video: Like (never a photo or a folder), then Add
			playable && (liked => ({ label: liked ? 'Unlike' : 'Like', icon: liked ? 'heart' : 'heart-o', y: liked, sub: liked ? 'In Liked' : undefined, fn: () => setLike(p, !liked, 'menu') }))(isLiked(p)),
			(isDir || playable) && { label: 'Add to playlist…', icon: isDir ? 'playlist' : 'pl-add', fn: () => addToPlaylist([p], isDir ? it.name : titleFor(p)) },
			isDir && canHide && { label: it.hidden ? 'Unhide folder' : 'Hide folder', icon: 'eye', sub: it.hidden ? 'Listed on the handheld again' : 'Left out of the handheld’s lists', fn: () => setHidden([p], !it.hidden, refresh) },
			{ label: 'Rename…', icon: 'pencil', fn: () => renamePath(p, refresh) },
			{ label: 'Move…', icon: 'move', fn: () => openModal(close => html`<${MovePicker} paths=${[p]} start=${path} onClose=${close} done=${refresh} />`) },
			!isDir && { label: 'Download', icon: 'download', fn: () => downloadPaths([p]) },
			{ label: 'Delete', icon: 'trash', danger: true, fn: () => deletePaths([p], refresh, byName) }]);
	}
	const playOn = (target, paths) => playPaths(paths, store.knownFolders, target);
	const nowV = view(), nowPath = nowV.has ? nowV.path : '';
	function rowClick(it) {
		if (lp.current.fired) { lp.current.fired = false; return; }
		const p = full(it.name);
		if (showSel && !it.dir) return toggle(p);
		if (showSel && it.dir && !pick) return toggle(p);
		if (it.dir) return go(filesHash(p));
		if (it.kind === 'photo') { const ph = items.filter(i => i.kind === 'photo').map(i => full(i.name)); return set({ lightbox: { items: ph, i: ph.indexOf(p) } }); }
		if (it.kind === 'audio' || it.kind === 'video') return playHere(t => playPaths([p], store.knownFolders, t, true), p, it.playable);
		rowSheet(it);
	}
	const lpStart = (e, it) => { lp.current.mouse = e.pointerType === 'mouse'; lp.current.fired = false; lp.current.x = e.clientX; lp.current.y = e.clientY; clearTimeout(lp.current.t); if (e.pointerType === 'mouse') return; lp.current.t = setTimeout(() => { lp.current.fired = true; toggle(full(it.name)); navigator.vibrate && navigator.vibrate(10); }, 500); };
	const lpMove = e => { if (Math.hypot(e.clientX - lp.current.x, e.clientY - lp.current.y) > 10) clearTimeout(lp.current.t); };
	const lpEnd = () => clearTimeout(lp.current.t);

	const crumbs = path.split('/').filter(Boolean);
	const upload = ents => startUpload(ents, path);
	const onDrop = e => { e.preventDefault(); e.stopPropagation(); setOver(false); set({ dropping: false, overZone: false }); upload_(e.dataTransfer); };
	const upload_ = dt => entriesFromDrop(dt).then(upload);
	const free = data && data.free;
	const freeCls = free != null && free < 200e6 ? 'crit' : free != null && free < 1e9 ? 'low' : '';
	const folderName = crumbs.length > 1 ? crumbs[crumbs.length - 1] : rootName(crumbs[0]);

	if (!path) {
		if (!roots) return html`<div class="page"><${Skeleton} /></div>`;
		return html`<div class="page"><div class="page-head"><h1>Files</h1></div><ul class="flist">${roots.map(r => html`<li class="frow"><button class="main-btn" onClick=${() => go(filesHash(r.path))}><${Icon} n="card" k=${3} cls="gl folder" /><span class="grow"><div class="fn">${r.name}</div><div class="fm">${fmtSize(r.free)} free</div></span></button></li>`)}</ul></div>`;
	}
	const addPick = async () => {
		try { await plEdit(pick.id, 'add', { p: selected }); toast('Added to ' + pick.name); const id = pick.id; set({ pick: null, sel: new Set() }); go('#/playlists/' + encodeURIComponent(id)); }
		catch (e) { toast(e.message); }
	};
	return html`<div class=${'page ' + (showSel ? 'selmode' : '')}>
		${pick && html`<div class="pickbar"><span class="t">Adding to ${pick.name} · ${selCount} selected</span><button class="btn primary sm" disabled=${!selCount} onClick=${addPick}>Add</button><button class="btn sm" onClick=${() => { const id = pick.id; set({ pick: null, sel: new Set() }); go('#/playlists/' + encodeURIComponent(id)); }}>Cancel</button></div>`}
		<div class="crumbs"><nav aria-label="Folder path">${crumbs.map((sg, i) => html`${i > 0 && html`<span class="sep" aria-hidden="true">›</span>`}<button aria-current=${i === crumbs.length - 1 ? 'page' : undefined} onClick=${() => go(filesHash(crumbs.slice(0, i + 1).join('/')))}>${i === 0 ? rootName(sg) : sg}</button>`)}</nav>
			${free != null && html`<span class="free ${freeCls}">${freeCls ? 'Low on space · ' : ''}${fmtSize(free)} free</span>`}</div>
		${!pick && html`<div class="toolbar">
			<button class="btn primary" onClick=${() => fileIn.current.click()}><${Icon} n="up" k=${2} />Upload</button>
			${!touchOnly && html`<button class="btn" onClick=${() => dirIn.current.click()}><${Icon} n="folder" k=${2} />Upload folder</button>`}
			<button class="btn" onClick=${() => nameDialog({ title: 'New folder', label: 'Folder name', ok: 'Create', submit: async n => { n = n.trim(); const bad = nameError(n); if (bad) return bad; try { await post('/api/mkdir', { p: path, name: n }); markFresh(joinPath(path, n)); load(true); return ''; } catch (e) { return e.message; } } })}><${Icon} n="plus" k=${2} />New folder</button>
			<button class="btn ${selMode ? 'primary' : ''}" aria-pressed=${selMode} onClick=${() => { setSelMode(!selMode); if (selMode) setSel([]); }}>${selMode ? 'Done' : 'Select'}</button>
			${canHide && html`<button class="btn tb-more" aria-label="More for this folder" aria-haspopup="dialog" onClick=${() => openSheet(folderName, 'folder', [{ label: 'Show hidden folders', icon: 'eye', on: !!showHidden, sub: showHidden ? 'On: the folders hidden on the handheld are listed, dimmed' : 'Off: the folders hidden on the handheld are left out', fn: () => setShowHidden(!showHidden) }])}><${MoreDots} /></button>`}
			<input type="file" multiple hidden ref=${fileIn} onChange=${e => { upload(entriesFromInput(e.target.files)); e.target.value = ''; }} />
			<input type="file" hidden webkitdirectory ref=${dirIn} onChange=${e => { upload(entriesFromInput(e.target.files)); e.target.value = ''; }} />
		</div>`}
		${!pick && html`<div class="dropzone ${over ? 'over' : ''}" onDragEnter=${e => { e.preventDefault(); setOver(true); set({ overZone: true }); }} onDragOver=${e => { e.preventDefault(); setOver(true); store.overZone = true; }} onDragLeave=${e => { if (!e.currentTarget.contains(e.relatedTarget)) { setOver(false); set({ overZone: false }); } }} onDrop=${onDrop}>
			<${Icon} n="up" k=${3} />Drop files or folders here to upload to ${folderName}</div>`}
		${err ? html`<${Empty} icon="alert" title="Couldn't open this folder" sub=${err} button="Try again" onClick=${() => load(false)} />` :
			!data ? (slow ? html`<${Skeleton} />` : null) :
			items.length === 0 ? html`<${Empty} icon="folder" title="This folder is empty" sub=${touchOnly ? 'Upload music, videos or photos from this device.' : 'Drop files here, or use Upload.'} button=${pick ? undefined : 'Upload'} onClick=${() => fileIn.current && fileIn.current.click()} />` : html`
			<div class="thead" aria-hidden="true"><span class="nm">Name</span><span class="sz">Size</span><span class="md">Modified</span></div>
			<ul class="flist">${items.map(it => { const p = full(it.name), on = sel.has(p); return html`<li class="frow ${on ? 'sel' : ''} ${s.fresh[p] ? 'fresh' : ''} ${it.hidden ? 'hiddenf' : ''} ${p === nowPath ? 'nowp' : ''}" key=${it.name}>
				<button class="chk ib" aria-label=${(on ? 'Deselect ' : 'Select ') + it.name} aria-pressed=${on} onClick=${() => toggle(p)}><${Icon} n=${on ? 'box-on' : 'box'} k=${2} /></button>
				<button class="main-btn" onClick=${() => rowClick(it)} onPointerDown=${e => lpStart(e, it)} onPointerMove=${lpMove} onPointerUp=${lpEnd} onPointerCancel=${lpEnd} onContextMenu=${e => { e.preventDefault(); if (lp.current.mouse !== false && !lp.current.fired) rowSheet(it); }}>
					<${Icon} n=${it.dir ? 'folder' : p === nowPath ? 'play' : GLYPH[it.kind] || 'info'} k=${3} cls=${'gl ' + (it.dir ? 'folder' : p === nowPath ? 'now' : '')} label=${p === nowPath ? 'Playing' : undefined} />
					<span class="grow"><div class="fn clamp2">${it.name}${it.hidden && html`<span class="hidmark" title="Hidden on the handheld"><${Icon} n="eye" k=${2} label="Hidden" /></span>`}${!it.dir && (it.kind === 'audio' || it.kind === 'video') && isLiked(p) && html`<span class="likemark" title="Liked"><${Icon} n="heart" k=${1} label="Liked" /></span>`}</div><div class="fm">${it.hidden ? 'Hidden · ' : ''}${it.dir ? itemsText(it.items, it.itemsMore) : fmtSize(it.size)}</div></span>
					<span class="cols"><span class="sz">${it.dir ? itemsText(it.items, it.itemsMore) : fmtSize(it.size)}</span><span class="md">${fmtDate(it.mtime)}</span></span>
				</button>
				<button class="ib more" aria-label=${'More about ' + it.name} onClick=${() => rowSheet(it)}><${MoreDots} /></button></li>`; })}</ul>`}
		${selCount > 0 && !pick && html`<div class="selbar" role="toolbar" aria-label="Selection">
			<div class="cnt"><span aria-live="polite">${selCount} selected</span><span style="display:flex;gap:var(--s2)">${selCount < items.length ? html`<button class="btn sm ghost" onClick=${() => setSel(items.map(i => full(i.name)))}>All</button>` : html`<button class="btn sm ghost" onClick=${() => setSel([])}>None</button>`}<button class="ib" aria-label="Clear selection" onClick=${() => { setSel([]); setSelMode(false); }} style="width:36px;height:36px"><${Icon} n="cross" k=${2} /></button></span></div>
			<button class="sb" onClick=${() => playOn(s.target, selected)}><${Icon} n="play" k=${2} />Play</button>
			<button class="sb" onClick=${() => addToPlaylist(selected, selCount === 1 ? titleFor(selected[0]) : plural(selCount, 'item', 'items'))}><${Icon} n="playlist" k=${2} />Add</button>
			<button class="sb" onClick=${() => openModal(close => html`<${MovePicker} paths=${selected} start=${path} onClose=${close} done=${refresh} />`)}><${Icon} n="move" k=${2} />Move</button>
			<button class="sb" onClick=${() => downloadPaths(selected)}><${Icon} n="download" k=${2} />Download</button>
			${canHide && selFolders.length > 0 && html`<button class="sb" onClick=${() => setHidden(selFolders, !selAllHidden, refresh)}><${Icon} n="eye" k=${2} />${selAllHidden ? 'Unhide' : 'Hide'}</button>`}
			<button class="sb danger" onClick=${() => deletePaths(selected, refresh, byName)}><${Icon} n="trash" k=${2} />Delete</button></div>`}
	</div>`;
}

function Lightbox() {
	const lb = store.lightbox, sw = useRef(null);
	const { items, i } = lb, p = items[i];
	const step = d => set({ lightbox: { items, i: (i + d + items.length) % items.length } });
	const close = () => set({ lightbox: null });
	const ref = useRef();
	useEffect(() => {
		const prev = document.activeElement; ref.current && ref.current.focus();
		return () => prev && prev.focus && prev.focus();
	}, []);
	useEffect(() => {
		const k = e => { if (e.key === 'Escape') close(); if (e.key === 'ArrowLeft') step(-1); if (e.key === 'ArrowRight') step(1); };
		addEventListener('keydown', k); return () => removeEventListener('keydown', k);
	});
	// a sideways swipe moves to the next or previous photo
	const down = e => { sw.current = { x: e.clientX, y: e.clientY }; };
	const up = e => { const a = sw.current; sw.current = null; if (!a || items.length < 2) return; const dx = e.clientX - a.x, dy = e.clientY - a.y; if (Math.abs(dx) > 48 && Math.abs(dx) > Math.abs(dy) * 1.5) step(dx < 0 ? 1 : -1); };
	const show = async () => { try { await post('/api/play', { p }); toast('Showing it on your handheld'); } catch (e) { toast(e.status === 0 ? "Couldn't show it on your handheld" : errMsg(e)); } };
	return html`<div class="lightbox" role="dialog" aria-modal="true" aria-label=${baseName(p)} tabindex="-1" ref=${ref} onPointerDown=${down} onPointerUp=${up} onPointerCancel=${() => sw.current = null} onClick=${e => e.target === e.currentTarget && close()}>
		<img src="/api/file?p=${encodeURIComponent(p)}" alt=${baseName(p)} draggable="false" />
		<div class="lb-top"><span class="grow ell">${baseName(p)}${items.length > 1 ? ' · ' + (i + 1) + ' / ' + items.length : ''}</span>
			<button class="btn sm ghost" onClick=${show}><${Icon} n="card" k=${2} />Show on handheld</button>
			<button class="ib" aria-label="Close" onClick=${close}><${Icon} n="cross" k=${2} /></button></div>
		${items.length > 1 && html`<button class="nav l" aria-label="Previous photo" onClick=${() => step(-1)}><${Icon} n="left" k=${4} /></button><button class="nav r" aria-label="Next photo" onClick=${() => step(1)}><${Icon} n="right" k=${4} /></button>`}</div>`;
}

/* ================= playlists ================= */
let uidSeq = 0;
function Playlists() { return store.route.id ? html`<${PlaylistView} id=${store.route.id} key=${store.route.id} />` : html`<${PlaylistList} />`; }

function PlaylistList() {
	const s = useStore(), [list, setList] = useState(null), [err, setErr] = useState('');
	const load = () => get('/api/playlists').then(r => { setList(r.playlists || []); setErr(''); }).catch(e => setErr(errMsg(e)));
	useEffect(() => { load(); }, [s.epoch, s.plTick]);
	const slow = useLater(!list && !err, 300);
	const open = id => go('#/playlists/' + encodeURIComponent(id));
	const sheet = p => openSheet(p.name, 'playlist', [
		{ label: 'Play on handheld', icon: 'card', fn: () => playFrom(p, 'handheld', false) },
		{ label: 'Play here', icon: 'network', disabled: NO_HERE, sub: NO_HERE ? NO_HERE_WHY : undefined, fn: () => playFrom(p, 'browser', false) },
		{ label: 'Shuffle play', icon: 'shuffle', fn: () => playFrom(p, store.target, true) },
		{ label: 'Rename…', icon: 'pencil', fn: () => nameDialog({ title: 'Rename playlist', label: 'Playlist name', initial: p.name, ok: 'Rename', select: true, submit: async n => { const m = validPlName(n, list, p.id); if (m) return m; try { await post('/api/playlists/rename', { id: p.id, name: n.trim() }); load(); return ''; } catch (e) { return e.message; } } }) },
		{ label: 'Delete', icon: 'trash', danger: true, fn: async () => { if (await ask('Delete ' + p.name + '?', 'Its files stay where they are.', { buttons: [{ label: 'Cancel', value: null }, { label: 'Delete', value: true, cls: 'danger' }] })) { try { await post('/api/playlists/delete', { id: p.id }); load(); } catch (e) { toast(e.message); } } } }]);
	async function playFrom(p, target, shuffle) {
		if (!userTap()) return;
		try { const r = await get('/api/playlist', { id: p.id }); await playPlaylist(p.id, r.name, r.items, 0, shuffle, target, true); } catch (e) { errToast(e); }
	}
	const create = () => newPlaylistDialog((id) => open(id));
	// Liked: built in, pinned in a block of its own; played, never renamed or deleted
	const liked = list && list.find(p => p.builtin), rest = list ? list.filter(p => !p.builtin) : [];
	const likedSheet = p => openSheet('Liked', 'heart', [
		{ label: 'Play on handheld', icon: 'card', disabled: !p.count, fn: () => playFrom(p, 'handheld', false) },
		{ label: 'Play here', icon: 'network', disabled: NO_HERE || !p.count, sub: NO_HERE ? NO_HERE_WHY : undefined, fn: () => playFrom(p, 'browser', false) },
		{ label: 'Shuffle play', icon: 'shuffle', disabled: !p.count, fn: () => playFrom(p, store.target, true) }]);
	if (err) return html`<div class="page"><${Empty} icon="alert" title="Couldn't load playlists" sub=${err} button="Try again" onClick=${load} /></div>`;
	return html`<div class="page"><div class="page-head"><h1>Playlists</h1>${rest.length > 0 && html`<button class="btn primary" onClick=${create}><${Icon} n="plus" k=${2} />New playlist</button>`}</div>
		${liked && html`<ul class="flist pinned" style="list-style:none"><li class="plrow liked"><button class="main-btn" onClick=${() => open(liked.id)}><${Icon} n="heart" k=${3} cls="gl" /><span class="grow"><div class="nm ell">${liked.name || 'Liked'}</div><div class="ct">${liked.count ? plural(liked.count, 'item', 'items') + ' · newest first' : 'Tap ♥ on anything playing'}</div></span></button>
			<button class="ib" aria-label="More about Liked" aria-haspopup="dialog" onClick=${() => likedSheet(liked)}><${MoreDots} /></button></li></ul>`}
		${!list ? (slow ? html`<${Skeleton} />` : null) : rest.length === 0 ? html`<${Empty} icon="playlist" title="No playlists yet" sub="Make one, then add songs and videos from Files." button="New playlist" onClick=${create} />` : html`
		<ul class="flist" style="list-style:none">${rest.map(p => html`<li class="plrow" key=${p.id}><button class="main-btn" onClick=${() => open(p.id)}><${Icon} n="playlist" k=${3} cls="gl" /><span class="grow"><div class="nm ell">${p.name}</div><div class="ct">${plural(p.count, 'item', 'items')}</div></span></button>
			<button class="ib" aria-label=${'More about ' + p.name} onClick=${() => sheet(p)}><${MoreDots} /></button></li>`)}</ul>`}</div>`;
}

function PlaylistView({ id }) {
	const s = useStore();
	const [pl, setPl] = useState(null), [err, setErr] = useState(''), [selMode, setSelMode] = useState(false), [sel, setSel] = useState(new Set()), [kb, setKb] = useState(null), [live, setLive] = useState('');
	const revRef = useRef(''), listRef = useRef(), itemsRef = useRef([]), sortable = useRef(null);
	const items = pl ? pl.items : [];
	// Liked: newest first, its order its own (no grips, no Add files); Remove there is Unlike
	const builtin = id === LIKED_ID || !!(pl && pl.builtin);
	itemsRef.current = items;
	const load = useCallback(async quiet => {
		try {
			const r = await get('/api/playlist', { id });
			if (quiet && revRef.current && r.rev !== revRef.current && id !== LIKED_ID) toast(r.name + ' changed on another device');
			// a like from elsewhere: its row comes in at the top, lit
			const before = quiet && id === LIKED_ID ? new Set(itemsRef.current.map(x => x.p || x.net)) : null;
			revRef.current = r.rev; setPl({ ...r, items: (r.items || []).map(x => ({ ...x, uid: ++uidSeq, fresh: !!before && !before.has(x.p || x.net) })) }); setErr(''); setSel(new Set());
		} catch (e) { if (e.status === 404) { go('#/playlists'); } else setErr(errMsg(e)); }
	}, [id]);
	useEffect(() => { load(false); }, [id]);
	useEffect(() => { if (pl) load(true); else if (err && s.epoch) load(false); }, [s.plTick, s.epoch]);
	const slow = useLater(!pl && !err, 300);

	async function edit(act, params, okMsg) {
		try { const r = await plEdit(id, act, params, revRef.current); revRef.current = r.rev; return r; }
		catch (e) { if (e.body && e.body.code === 'rev') { toast((pl && pl.name || 'This playlist') + ' changed on another device'); } else toast(e.message); load(false); return null; }
	}
	async function moveTo(from, newIdx) {
		if (from === newIdx) return;
		const a = itemsRef.current.slice(), [m] = a.splice(from, 1); a.splice(newIdx, 0, m);
		setPl(p => ({ ...p, items: a }));
		await edit('move', { i: [from], to: newIdx > from ? newIdx + 1 : newIdx });
	}
	// SortableJS moves DOM nodes; put the node back and let Preact reorder from state
	useEffect(() => {
		if (!listRef.current || sortable.current || id === LIKED_ID) return;
		sortable.current = window.Sortable.create(listRef.current, { handle: '.grip', animation: 150, delay: 150, delayOnTouchOnly: true, touchStartThreshold: 4, ghostClass: 'sortable-ghost', chosenClass: 'sortable-chosen',
			onEnd: evt => { const { item, from, oldIndex, newIndex } = evt; from.removeChild(item); from.insertBefore(item, from.children[oldIndex] || null); moveTo(oldIndex, newIndex); } });
		return () => { sortable.current && sortable.current.destroy(); sortable.current = null; };
	}, [!!pl && items.length > 0]);
	useEffect(() => { if (kb) { const g = document.querySelector('[data-uid="' + kb.uid + '"] .grip'); if (g && document.activeElement !== g) g.focus(); } }, [items, kb]);

	function gripKey(e, idx, it) {
		const n = items.length;
		if (e.key === ' ' || e.key === 'Enter') {
			e.preventDefault();
			if (!kb) { setKb({ uid: it.uid, from: idx, orig: items }); setLive(`Picked up ${it.title}, position ${idx + 1} of ${n}. Use the arrow keys, then Space to drop.`); }
			else { const to = items.findIndex(x => x.uid === kb.uid); setKb(null); setLive(`Dropped at position ${to + 1} of ${n}`); if (to !== kb.from) edit('move', { i: [kb.from], to: to > kb.from ? to + 1 : to }); }
		} else if (kb && (e.key === 'ArrowUp' || e.key === 'ArrowDown')) {
			e.preventDefault();
			const to = idx + (e.key === 'ArrowUp' ? -1 : 1); if (to < 0 || to >= n) return;
			const a = items.slice(); [a[idx], a[to]] = [a[to], a[idx]]; setPl({ ...pl, items: a }); setLive(`Position ${to + 1} of ${n}`);
		} else if (kb && e.key === 'Escape') { e.preventDefault(); e.stopPropagation(); setPl({ ...pl, items: kb.orig }); setKb(null); setLive('Cancelled'); }
	}
	async function remove(uids) {
		const removed = items.map((it, i) => ({ ...it, i })).filter(x => uids.has(x.uid));
		if (!removed.length) return;
		const r = await edit('remove', { i: removed.map(x => x.i) }); if (!r) return;
		setSel(new Set()); setSelMode(false);
		if (!reduceMotion()) { removed.forEach(x => { const row = document.querySelector('.pitem[data-uid="' + x.uid + '"]'); if (row) row.classList.add('leaving'); }); await new Promise(res => setTimeout(res, 200)); }
		setPl(p => ({ ...p, items: p.items.filter(x => !uids.has(x.uid)) }));
		if (builtin) {
			// Undo likes them again, each back in its place (in order, so the indexes hold); a network one can't be
			// liked from here
			const back = removed.filter(x => x.p);
			toast(removed.length === 1 ? 'Removed from Liked' : 'Removed ' + removed.length + ' from Liked', back.length ? { label: 'Undo', fn: async () => {
				try { for (const x of back) await post('/api/like', { p: x.p, on: 1, at: x.i }); } catch (e) { errToast(e); }
				load(false);
			} } : null, 6000);
			return;
		}
		toast('Removed ' + plural(removed.length, 'item', 'items'), { label: 'Undo', fn: async () => {
			try { let rev = revRef.current; for (const x of removed) { const r2 = await plEdit(id, 'add', { p: [x.p], at: x.i }, rev); rev = r2.rev; revRef.current = rev; } load(false); } catch (e) { toast(e.message); load(false); }
		} }, 6000);
	}
	const playItem = i => playHere(t => playPlaylist(id, pl.name, items, i, false, t, true, true), items[i].p);
	// (a network item plays on the handheld only)
	const playLiked = i => items[i].p ? playItem(i) : playPlaylist(id, pl.name, items, i, false, 'handheld', true, true);
	const addFiles = () => { set({ pick: { id, name: pl.name }, sel: new Set() }); go(filesHash(store.lastDir || (store.roots && store.roots[0].path) || '')); };
	const total = items.reduce((a, x) => a + (x.duration > 0 ? x.duration : 0), 0);
	const header = pl && html`<div class="page-head nowrap"><button class="ib" aria-label="Back to playlists" onClick=${() => go('#/playlists')}><${Icon} n="back" k=${3} /></button>
		<div class="grow"><h1 class="clamp2">${pl.name}</h1><div class="dim" style="font-size:14px">${builtin && !items.length ? 'Nothing yet' : plural(items.length, 'item', 'items') + (total > 0 ? ' · ' + fmtTime(total) : '') + (builtin ? ' · newest first' : '')}</div></div>
		<button class="ib" aria-label=${'More about ' + pl.name} aria-haspopup="dialog" onClick=${() => menu()}><${MoreDots} /></button></div>`;
	const menu = () => builtin ? openSheet(pl.name, 'heart', [{ label: 'Select…', icon: 'box', sub: 'To unlike several at once', disabled: !items.length, fn: () => setSelMode(true) }]) : openSheet(pl.name, 'playlist', [
		{ label: 'Rename…', icon: 'pencil', fn: () => nameDialog({ title: 'Rename playlist', label: 'Playlist name', initial: pl.name, ok: 'Rename', select: true, submit: async n => { let others = []; try { others = (await get('/api/playlists')).playlists; } catch (e) {} const m = validPlName(n, others, id); if (m) return m; try { await post('/api/playlists/rename', { id, name: n.trim() }); load(false); return ''; } catch (e) { return e.message; } } }) },
		{ label: 'Remove duplicates', icon: 'refresh', fn: async () => { const before = items.length, r = await edit('dedupe', {}); if (r) { await load(false); toast(r.count != null && before - r.count > 0 ? 'Removed ' + plural(before - r.count, 'duplicate', 'duplicates') : 'No duplicates'); } } },
		{ label: 'Select…', icon: 'box', fn: () => setSelMode(true) },
		{ label: 'Delete', icon: 'trash', danger: true, fn: async () => { if (await ask('Delete ' + pl.name + '?', 'Its files stay where they are.', { buttons: [{ label: 'Cancel', value: null }, { label: 'Delete', value: true, cls: 'danger' }] })) { try { await post('/api/playlists/delete', { id }); go('#/playlists'); } catch (e) { toast(e.message); } } } }]);
	const rowMenu = (it, i) => openSheet(it.title, GLYPH[kindOfName(it.p)] || 'note', [
		{ label: 'Play from here', icon: 'play', disabled: it.missing, fn: () => playPlaylist(id, pl.name, items, i, false) },
		{ label: 'Remove from playlist', icon: 'trash', danger: true, fn: () => remove(new Set([it.uid])) }]);
	const likedMenu = (it, i) => openSheet(it.title, GLYPH[it.kind] || 'note', it.missing ? [{ label: 'Unlike', icon: 'heart-o', sub: 'The file is gone from the card', fn: () => remove(new Set([it.uid])) }] : [
		{ label: 'Play from here', icon: 'play', fn: () => playLiked(i) },
		{ label: 'Play in this browser', icon: 'network', disabled: NO_HERE || !it.p, sub: NO_HERE ? NO_HERE_WHY : it.p ? undefined : 'Network files play on the handheld only', fn: () => playPlaylist(id, pl.name, items, i, false, 'browser') },
		{ label: 'Unlike', icon: 'heart-o', fn: () => remove(new Set([it.uid])) },
		it.p && { label: 'Add to playlist…', icon: 'pl-add', fn: () => addToPlaylist([it.p], it.title) },
		it.p && { label: 'Show in Files', icon: 'folder', fn: () => showInFiles(it.p) }]);
	if (err) return html`<div class="page"><${Empty} icon="alert" title="Couldn't open this playlist" sub=${err} button="Try again" onClick=${() => load(false)} /></div>`;
	if (!pl) return html`<div class="page">${slow ? html`<${Skeleton} />` : null}</div>`;
	return html`<div class=${'page ' + (selMode ? 'pl-sel' : '')}>${header}
		${items.length === 0 && builtin ? html`<${Empty} icon="heart-o" title="Nothing liked yet" sub="Tap the heart on anything playing, here or on your handheld. It shows up here, newest first." button="Go to Playing" onClick=${() => go('#/playing')} />` : items.length === 0 ? html`<${Empty} icon="playlist" title=${pl.name + ' is empty'} sub="Add songs and videos from Files." button="Add files" onClick=${addFiles} />` : html`
		<div class="pl-actions"><button class="btn primary" onClick=${() => playPlaylist(id, pl.name, items, 0, false)}><${Icon} n="play" k=${2} />Play</button>
			<button class="btn" onClick=${() => playPlaylist(id, pl.name, items, 0, true)}><${Icon} n="shuffle" k=${2} />Shuffle</button>
			${!builtin && html`<button class="btn" onClick=${addFiles}><${Icon} n="plus" k=${2} />Add files</button>`}</div>
		<div class="flist" ref=${listRef} role="list">${items.map((it, i) => { const k = kindOfName(it.p), on = sel.has(it.uid); return html`<div class="pitem ${on ? 'sel' : ''} ${kb && kb.uid === it.uid ? 'picked' : ''} ${it.missing ? 'gone' : ''} ${builtin ? 'nogrip' : ''} ${it.fresh ? 'fresh' : ''}" role="listitem" key=${it.uid} data-uid=${it.uid}>
			<button class="chk" aria-label=${(on ? 'Deselect ' : 'Select ') + it.title} onClick=${() => setSel(x => { const n = new Set(x); n.has(it.uid) ? n.delete(it.uid) : n.add(it.uid); return n; })}><${Icon} n=${on ? 'box-on' : 'box'} k=${2} /></button>
			${!builtin && html`<button class="grip" aria-label=${'Reorder ' + it.title} aria-roledescription="drag handle" onKeyDown=${e => gripKey(e, i, it)}><${Icon} n="grip" k=${3} /></button>`}
			<button class="tt" onClick=${() => selMode ? setSel(x => { const n = new Set(x); n.has(it.uid) ? n.delete(it.uid) : n.add(it.uid); return n; }) : it.missing ? toast('That file is gone from the card') : builtin ? playLiked(i) : playItem(i)} onContextMenu=${e => { if (matchMedia('(hover: hover)').matches) { e.preventDefault(); (builtin ? likedMenu : rowMenu)(it, i); } }}>
				<div class="fn clamp2">${titleFor(it.p, it.title)}</div><div class="fm ell">${it.net ? html`<${Icon} n="network" k=${1} />${baseName(dirName(it.net))}` : baseName(dirName(it.p))}${it.duration > 0 ? ' · ' + fmtTime(it.duration) : ''}</div></button>
			${it.missing && html`<span class="chip red">Gone</span>`}
			${builtin && html`<button class="ib like on" aria-label=${'Unlike ' + it.title} title="Unlike" onClick=${() => remove(new Set([it.uid]))}><${Icon} n="heart" k=${2} /></button>`}
			<button class="ib" aria-label=${'More about ' + it.title} onClick=${() => (builtin ? likedMenu : rowMenu)(it, i)}><${MoreDots} /></button></div>`; })}</div>
		${selMode && html`<div class="selbar"><div class="cnt"><span>${sel.size} selected</span><span style="display:flex;gap:var(--s2)">${sel.size < items.length ? html`<button class="btn sm ghost" onClick=${() => setSel(new Set(items.map(x => x.uid)))}>All</button>` : html`<button class="btn sm ghost" onClick=${() => setSel(new Set())}>None</button>`}<button class="ib" aria-label="Done selecting" style="width:36px;height:36px" onClick=${() => { setSelMode(false); setSel(new Set()); }}><${Icon} n="cross" k=${2} /></button></span></div>
			<button class="sb danger" disabled=${!sel.size} onClick=${() => remove(sel)}><${Icon} n=${builtin ? 'heart-o' : 'trash'} k=${2} />${builtin ? 'Unlike' : 'Remove'}</button></div>`}`}
		<div class="sr" aria-live="assertive">${live}</div></div>`;
}

/* ================= upload panel ================= */
function UploadPanel() {
	const u = store.upload, items = u.items; if (!items.length) return null;
	const active = items.filter(i => i.state === 'up' || i.state === 'queued'), waiting = items.filter(i => i.state === 'waiting'), done = items.filter(i => i.state === 'done'), failed = items.filter(i => i.state === 'failed');
	const total = items.reduce((a, i) => a + i.size, 0), loaded = items.reduce((a, i) => a + (i.state === 'done' ? i.size : i.loaded), 0);
	const title = waiting.length && !active.length ? 'Waiting for your handheld · ' + plural(waiting.length, 'file', 'files')
		: active.length || waiting.length ? `Uploading ${Math.min(done.length + 1, items.length)} of ${items.length} · ${fmtSize(total - loaded)} left`
		: failed.length ? `${done.length} uploaded · ${failed.length} failed` : 'Uploaded ' + plural(done.length, 'file', 'files');
	const allDone = !active.length && !waiting.length && !failed.length;
	return html`<aside class="upanel ${u.min ? 'min' : ''} ${allDone ? 'done' : ''}" aria-label="Uploads"><div class="totalbar"><i style="width:${total ? loaded / total * 100 : 0}%"></i></div>
		<header><div class="tt" aria-live="polite">${allDone && html`<${Icon} n="check" k=${2} cls="ok" />`}${title}</div>
			${active.length || waiting.length ? html`<button class="btn sm ghost" onClick=${cancelAllUploads}>Cancel all</button>` : html`<button class="btn sm" onClick=${() => set({ upload: { items: [], min: false } })}>Done</button>`}
			<button class="ib" aria-label=${u.min ? 'Show uploads' : 'Fold uploads'} aria-expanded=${!u.min} onClick=${() => { clearTimeout(collapseTimer); set({ upload: { ...u, min: !u.min } }); }}><${Icon} n=${u.min ? 'up' : 'down'} k=${2} /></button></header>
		<div class="items">${items.map(it => html`<div class="uitem" key=${it.id}><div class="nm"><div class="ell">${joinPath(it.rel, it.name)}</div>
			${it.state === 'failed' ? html`<div class="meta" style="color:var(--red)">${it.error}</div>` : html`<div class="meta">${fmtSize(it.size)}${it.state === 'queued' ? ' · next' : it.state === 'waiting' ? ' · waiting for your handheld' : ''}</div>`}
			${(it.state === 'up') && html`<div class="ubar"><i style="width:${it.size ? it.loaded / it.size * 100 : 0}%"></i></div>`}</div>
			${it.state === 'done' ? html`<${Icon} n="check" k=${2} cls="ok" label="Done" />` : it.state === 'failed' ? html`<button class="fail" onClick=${() => retryUpload(it)}>Failed · Retry</button>` : html`<span class="st">${it.state === 'up' ? Math.floor(it.loaded / (it.size || 1) * 100) + '%' : ''}</span>`}
			${it.state !== 'done' && html`<button class="ib" style="width:36px;height:36px" aria-label=${'Cancel ' + it.name} onClick=${() => cancelUpload(it)}><${Icon} n="cross" k=${2} /></button>`}</div>`)}</div></aside>`;
}

/* ================= down screen: the handheld closed, off, asleep or out of reach ================= */
// the logo, kept from the start: when the handheld is gone, nothing more can be fetched from it
let logoUrl = '/logo.svg';
fetch('/logo.svg').then(r => r.ok ? r.text() : '').then(t => { if (t) logoUrl = 'data:image/svg+xml;charset=utf-8,' + encodeURIComponent(t); }).catch(() => {});
const DOWN = {
	quit: ['Banana Player was closed on your handheld', 'Open Banana Player on your handheld. This page reconnects by itself.'],
	off: ['The web dashboard was turned off', 'Turn it on again on your handheld: START › Settings › Web dashboard. This page reconnects by itself.'],
	sleep: ['Your handheld is asleep', 'Open its lid to continue. This page reconnects by itself.'],
	none: ["Can't reach your handheld", 'It may be asleep, or off this Wi-Fi. This page reconnects by itself.'],
};
function DownScreen() {
	const s = store; useTick(1000);
	const [t, sub] = DOWN[s.downReason] || DOWN.none;
	const left = Math.max(0, Math.ceil((s.nextTry - Date.now()) / 1000));
	const status = s.probing || !left ? 'Trying again…' : 'Next try in ' + fmtTime(left);
	const ref = useRef();
	useEffect(() => { ref.current && ref.current.focus(); }, [s.downReason]);
	return html`<main class="down" aria-labelledby="down-t">
		<div class="down-box">
			<img src=${logoUrl} alt="" width="64" height="64" />
			<h1 id="down-t" tabindex="-1" ref=${ref}>${t}</h1>
			<p class="down-sub">${sub}</p>
			${B.meta && B.parkedPlaying && html`<div class="down-card"><${Icon} n=${B.meta.kind === 'video' ? 'film' : 'note'} k=${2} /><span><b>${B.meta.title}</b> was playing here. It stopped at ${fmtTime(B.parked)}.</span></div>`}
			<p class="down-status" role="status" aria-live="polite">${status}</p>
			<button class="btn" disabled=${s.probing} onClick=${() => probe()}>Try again</button>
		</div></main>`;
}

/* ================= shell ================= */
const NAV = [['playing', 'Playing', 'note', '#/playing'], ['files', 'Files', 'folder', '#/files'], ['playlists', 'Playlists', 'playlist', '#/playlists']];
async function signOut() { browserStop(); closeSSE(); try { await post('/api/logout'); } catch (e) {} set({ authed: false, signedOut: '', snap: null }); }

function MiniPlayer() {
	const v = view(); useTick(1000);
	if (!v.has || store.route.page === 'playing' || store.selecting) return null;
	const frac = v.duration ? Math.min(1, v.pos() / v.duration) : 0;
	return html`<div class="mini" onClick=${() => go('#/playing')} role="region" aria-label="Now playing">
		<div class="prog"><i style="width:${frac * 100}%"></i></div>
		<div class="art-sm"><${Cover} path=${v.cover} kind=${v.kind} k=${3} /></div>
		<div class="meta"><div class="t ell">${v.title}</div><div class="a ell">${v.artist || (v.here ? 'In this browser' : 'On handheld')}</div></div>
		<span class="chip-target" title=${v.here ? 'In this browser' : 'On handheld'}><${Icon} n=${v.here ? 'laptop' : 'card'} k=${2} label=${v.here ? 'In this browser' : 'On handheld'} /></span>
		<button class="ib play" aria-label=${v.paused ? 'Play' : 'Pause'} onClick=${e => { e.stopPropagation(); ctl.toggle(); }}><${Icon} n=${v.paused ? 'play' : 'pause'} k=${3} /></button></div>`;
}
// the desktop's player bar, on every page (on Playing too: the transport lives here, as on YouTube Music)
function PlayerBar() {
	const v = view(), s = store; useTick(1000);
	if (!v.has) return html`<div class="bar"><span class="dim grow" style="font-size:14px">Nothing playing</span><div class="bar-r"><${DeviceChip} cls="bar-chip" /></div></div>`;
	const off = s.reconnecting && !v.here, onPage = s.route.page === 'playing';
	return html`<div class="bar" role="region" aria-label="Player">
		<div class="bar-seek"><${Seek} view=${v} onSeek=${ctl.seek} disabled=${off} /></div>
		<div class="ctl"><button class="ib" aria-label="Previous" onClick=${ctl.prev} disabled=${off}><${Icon} n="prev" k=${3} /></button>
			<button class="ib play" aria-label=${v.paused ? 'Play' : 'Pause'} onClick=${ctl.toggle} disabled=${off}><${Icon} n=${v.paused ? 'play' : 'pause'} k=${3} /></button>
			<button class="ib" aria-label="Next" onClick=${ctl.next} disabled=${off}><${Icon} n="next" k=${3} /></button></div>
		<span class="tm">${fmtTime(v.pos())} / ${fmtTime(v.duration)}</span>
		<div class=${'meta' + (onPage ? ' here' : '')} onClick=${() => go('#/playing')} role=${onPage ? undefined : 'link'} aria-label=${onPage ? undefined : 'Open the player'}>
			<div class="art-sm"><${Cover} path=${v.kind === 'video' ? v.path : v.cover} kind=${v.kind} k=${3} /></div><div class="t-wrap"><div class="t ell">${v.title}</div><div class="a ell">${v.artist || targetLabel(s.target)}</div></div>
			${likable(v) && html`<div class="acts" onClick=${e => e.stopPropagation()}><${LikeBtn} v=${v} cls="ib" />
				<button class=${'ib' + (s.addOpen ? ' open' : '')} aria-label="Add to playlist" title="Add to playlist" aria-haspopup="dialog" aria-expanded=${!!s.addOpen} onClick=${e => addToPlaylist([v.path], v.title, e.currentTarget)}><${Icon} n="pl-add" k=${2} /></button></div>`}</div>
		<div class="bar-r">
			<button class=${'ib' + (v.loop !== 'no' ? ' on' : '')} aria-label=${loopName(v.loop)} title=${loopName(v.loop)} aria-pressed=${v.loop !== 'no'} onClick=${ctl.loop} disabled=${off}><${LoopIcon} loop=${v.loop} k=${2} /></button>
			<button class=${'ib' + (v.shuffled ? ' on' : '')} aria-label="Shuffle" title=${v.shuffled ? 'Shuffle: On' : 'Shuffle: Off'} aria-pressed=${!!v.shuffled} onClick=${ctl.shuffle} disabled=${off}><${Icon} n="shuffle" k=${2} /></button>
			<${DeviceChip} cls="bar-chip" />
			<${BarVolume} /></div></div>`;
}
function BarVolume() {
	const here = store.target === 'browser', sn = store.snap;
	const [val, hold] = useHeld(here ? B.vol : sn ? sn.volume : 0);
	const send = useMemo(() => throttled(v => post('/api/volume', { v }).catch(() => {}), 120), []);
	if ((here && isIOS) || (!here && sn && sn.volume < 0)) return null;
	const change = e => { const v = +e.target.value; hold(v); if (here) setBrowserVol(v); else send(v); };
	return html`<div class="bar-vol"><${Icon} n="speaker" k=${2} /><input type="range" min="0" max="100" value=${val} onInput=${change} aria-label=${here ? 'Browser volume' : 'Handheld volume'} aria-valuetext=${val + '%'} /></div>`;
}
function Toasts() {
	return html`<div class="toasts" role="status" aria-live="polite">${store.toasts.map(t => html`<div class=${'toast' + (t.out ? ' out' : '')} key=${t.id}><span class="grow">${t.text}</span>${t.action && html`<button onClick=${() => { set({ toasts: store.toasts.filter(x => x.id !== t.id) }); t.action.fn(); }}>${t.action.label}</button>`}</div>`)}</div>`;
}
function HelpOverlay() {
	const keys = [['Space', 'Play / pause'], ['← →', 'Back 5 s / forward 10 s'], ['Shift + ← →', 'Previous / next'], ['[ ]', 'Previous / next chapter'], ['L', 'Loop'], ['↑ ↓', 'Volume'], ['?', 'This help'], ['Esc', 'Close']];
	const close = () => set({ help: false });
	return html`<${Modal} title="Keyboard shortcuts" onClose=${close} actions=${html`<button class="btn primary" onClick=${close}>Close</button>`}><div class="helpkeys">${keys.map(([k, d]) => html`<kbd>${k}</kbd><span>${d}</span>`)}</div></${Modal}>`;
}

function Shell() {
	const s = useStore(), r = s.route, [menu, setMenu] = useState(false), v = view();
	// the liked files, for the hearts on rows and menus: again whenever a playlist changes anywhere
	useEffect(() => { loadLikes(); }, [s.plTick, s.epoch]);
	const desk = useMedia('(min-width: 1024px)');
	// something playing: on a phone or a tablet the player takes the whole screen; on a computer the menu folds to a rail
	const full = r.page === 'playing' && v.has && !desk, railed = r.page === 'playing' && v.has && desk;
	// drag and drop onto the whole page
	useEffect(() => {
		let depth = 0;
		const hasFiles = e => e.dataTransfer && [...e.dataTransfer.types].includes('Files');
		const enter = e => { if (hasFiles(e)) { e.preventDefault(); depth++; set({ dropping: true }); } };
		const over = e => { if (hasFiles(e)) e.preventDefault(); };
		const leave = e => { if (hasFiles(e) && --depth <= 0) { depth = 0; set({ dropping: false }); } };
		const drop = e => { if (!hasFiles(e)) return; e.preventDefault(); depth = 0; set({ dropping: false }); const dir = store.lastDir || (store.roots && store.roots[0] && store.roots[0].path); if (!dir) return; entriesFromDrop(e.dataTransfer).then(x => startUpload(x, dir)); };
		addEventListener('dragenter', enter); addEventListener('dragover', over); addEventListener('dragleave', leave); addEventListener('drop', drop);
		return () => { removeEventListener('dragenter', enter); removeEventListener('dragover', over); removeEventListener('dragleave', leave); removeEventListener('drop', drop); };
	}, []);
	// keys
	useEffect(() => {
		const key = e => {
			const t = e.target, tag = t.tagName, role = t.getAttribute && t.getAttribute('role');
			if (e.ctrlKey || e.metaKey || e.altKey || store.modals.length || store.lightbox) return;
			if (/^(INPUT|SELECT|TEXTAREA)$/.test(tag) && !(tag === 'INPUT' && t.type === 'range')) return;	// typing
			const slider = role === 'slider' || t.type === 'range';	// arrows belong to a focused slider
			const button = tag === 'BUTTON' || tag === 'A';	// Space belongs to a focused button
			const sn = store.snap, here = hereNow();
			if (e.key === 'Escape' && store.help) return set({ help: false });
			if (e.key === 'Escape' && store.sheetOpen && store.route.page === 'playing') return closePanel();
			if (e.key === '?') return set({ help: !store.help });
			if (e.key === ' ' && !button) { e.preventDefault(); ctl.toggle(); }
			else if ((e.key === 'ArrowLeft' || e.key === 'ArrowRight') && !slider && !t.closest('.eq-bars')) { e.preventDefault(); const fwd = e.key === 'ArrowRight'; if (e.shiftKey) fwd ? ctl.next() : ctl.prev(); else ctl.seekRel(fwd ? 10 : -5); }
			else if (e.key === '[' || e.key === ']') ctl.chapterStep(e.key === ']' ? 1 : -1);
			else if ((e.key === 'l' || e.key === 'L') && (sn || here)) ctl.loop();
			else if ((e.key === 'ArrowUp' || e.key === 'ArrowDown') && !slider && !button) {
				e.preventDefault();
				const cur = here ? B.vol : sn ? sn.volume : 0, nv = Math.max(0, Math.min(100, cur + (e.key === 'ArrowUp' ? 5 : -5)));
				if (here) setBrowserVol(nv); else { post('/api/volume', { v: nv }).catch(() => {}); if (sn) { sn.volume = nv; notify(); } }
			}
		};
		addEventListener('keydown', key); return () => removeEventListener('keydown', key);
	}, []);
	const page = r.page === 'files' ? html`<${Files} />` : r.page === 'playlists' ? html`<${Playlists} />` : html`<${NowPlaying} />`;
	const miniShown = v.has && r.page !== 'playing' && !s.selecting;
	const upMin = s.upload.items.length > 0 && s.upload.min;
	const nav = (k, h) => go(k === 'files' ? filesHash(store.lastDir || '') : h);
	// the Playing page has its own bar and controls: the small player (phone) and the bottom bar (desktop) would repeat them
	return html`<div class=${'shell' + (miniShown ? ' has-mini' : '') + (upMin ? ' has-strip' : '') + (r.page === 'playing' ? ' on-playing' : '') + (full ? ' fullplayer' + (s.sheetOpen ? ' sheet-open' : '') : '') + (railed ? ' railed' : '')}>
		<header class="topbar"><img class="logo32" src="/logo.svg" width="32" height="32" alt="" /><span class="title grow">Banana Player</span>
			${s.reconnecting && html`<span class="pill red" role="status">Offline</span>`}
			<button class="ib" aria-label="More" aria-haspopup="menu" aria-expanded=${menu} onClick=${() => setMenu(!menu)}><${MoreDots} /></button>
			${menu && html`<div class="menu" role="menu" onClick=${() => setMenu(false)}><button class="sheet-item" role="menuitem" onClick=${signOut}>Sign out</button></div>`}</header>
		<aside class="sidebar"><div class="side-logo"><img class="logo32" src="/logo.svg" width="32" height="32" alt="" /><span class="lbl">Banana Player</span></div>
			<nav class="side-nav" aria-label="Main">${NAV.map(([k, l, ic, h]) => html`<button aria-current=${r.page === k ? 'page' : undefined} title=${railed ? l : undefined} onClick=${() => nav(k, h)}><${Icon} n=${ic} k=${2} /><span class="lbl">${l}</span></button>`)}</nav>
			<div class="side-foot"><button class="ib rail-only" aria-label="Sign out" title="Sign out" onClick=${signOut}><${Icon} n="power" k=${2} /></button><div class="conn" title=${s.reconnecting ? 'Reconnecting…' : 'Handheld connected'}><span class=${'dot ' + (s.reconnecting ? 'off' : '')}></span><span class="lbl">${s.reconnecting ? 'Reconnecting…' : 'Handheld connected'}</span></div><button class="btn sm ghost rail-hide" onClick=${signOut}>Sign out</button><div class="rail-hide" style="font:400 12px/1.4 var(--text);color:var(--dim)">Unencrypted: use it on your home Wi-Fi</div></div></aside>
		<main class="main" id="main">${s.updated && !s.reconnecting && html`<div class="reconnecting updated" role="status"><span>Banana Player was updated</span><button class="btn sm primary" onClick=${() => location.reload()}>Reload</button></div>`}${s.reconnecting && html`<div class="reconnecting" role="status"><span class="dot off"></span>Reconnecting to your handheld…</div>`}${page}</main>
		<${MiniPlayer} />
		<nav class="tabs" aria-label="Main">${NAV.map(([k, l, ic, h]) => html`<button aria-current=${r.page === k ? 'page' : undefined} onClick=${() => nav(k, h)}><${Icon} n=${ic} k=${2} />${l}</button>`)}</nav>
		<${PlayerBar} />
		${s.upload.items.length > 0 && html`<${UploadPanel} />`}
		${s.lightbox && html`<${Lightbox} />`}
		${s.help && html`<${HelpOverlay} />`}
		${s.modals.map(m => html`<div class=${'mwrap' + (m.out ? ' out' : '')} key=${m.id}>${m.render(m.close)}</div>`)}
		${s.dropping && !s.overZone && html`<div class="dropover"><div>Drop to upload to ${dirLabel(store.lastDir || 'This card')}</div></div>`}
		<${Toasts} />
	</div>`;
}
const Fragment = ({ children }) => children;

/* ================= app and boot ================= */
function App() {
	const s = useStore();
	const slow = useLater(s.authed === null && s.conn !== 'down', 600);
	if (s.conn === 'down' && s.authed !== false) return html`<div class=${'downwrap' + (s.upload.items.length ? ' has-up' : '')}><${DownScreen} />${s.upload.items.length > 0 && html`<${UploadPanel} />`}<${Toasts} /></div>`;
	if (s.authed === null) return html`<main class="login"><img src="/logo.svg" alt="" /><h1>Banana Player</h1><p class="sub" aria-live="polite">${slow ? 'Connecting…' : ' '}</p></main>`;
	if (!s.authed) return html`<${Login} />`;
	return html`<${Shell} />`;
}
async function boot() {
	if (/^#pin=/.test(location.hash)) return pinFromHash();
	pingBoot();
	try { const st = await get('/api/state'); set({ authed: true }); C.okAt = Date.now(); applySnap(st); openSSE(); loadExtras(); }
	catch (e) {
		if (e.status === 401) set({ authed: false });
		else goDown('none');	// nothing to show yet: say so at once, and keep trying
	}
}
// headless and simulator checks: ?debug&do=sheet:queue,scroll:600,click:New folder (one step every 1.5 s, after boot)
if (location.search.includes('debug')) {
	const steps = (new URLSearchParams(location.search).get('do') || '').split(',').filter(Boolean);
	steps.forEach((st, i) => setTimeout(() => {
		const [k, ...rest] = st.split(':'), v = rest.join(':');
		if (k === 'sheet') showTab(v);
		else if (k === 'scroll') { const m = document.querySelector('.main'); if (m) m.scrollTop = +v; }
		else if (k === 'click') { const b = [...document.querySelectorAll('button')].find(x => x.offsetParent && (x.textContent.trim() === v || x.getAttribute('aria-label') === v)); if (b) b.click(); }
	}, 2500 + i * 1500));
}
if (location.search.includes('debug')) window.__bp = { store, set, closePanel, setTarget, continueHere, B, C, SUBS, startUpload, entriesFromDrop, browserPlay, toast, go, goDown, connFail, el };	// for the headless tests
// the icons first (their sizes come from the sprite), but never a blank page waiting for them
(async () => {
	let rendered = false;
	const go1 = () => { if (rendered) return; rendered = true; render(html`<${App} />`, document.getElementById('root')); boot(); };
	const late = setTimeout(go1, 1500);
	try {
		const { r, text } = await fetchT('/sprite.svg', {}, 10000);
		if (r.ok) { document.body.insertAdjacentHTML('afterbegin', text); for (const k in dimsCache) delete dimsCache[k]; if (rendered) notify(); }
	} catch (e) {}
	clearTimeout(late); go1();
})();
