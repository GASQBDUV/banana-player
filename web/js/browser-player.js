import { NO_HERE, NO_HERE_WHY, TAB_ID, lsGet, lsSet, notify, set, store } from './store.js';
import { closeSSE, openSSE } from './events.js';
import { errMsg, errToast, fetchT, get, post } from './api.js';
import { markTrack, toast } from './toasts.js';
import { baseName, cmpName, dirName, fmtTime, joinPath, kindOfName, plural, titleFor, userTap } from './util.js';
import { C } from './connection.js';
import { go, tapped } from './router.js';
import { markUsed } from './plshared.js';

/* ================= browser player ================= */
export const el = document.createElement('video');
el.playsInline = true; el.preload = 'auto'; el.controls = false;
document.getElementById('holder').appendChild(el);
// parked: unloaded when the connection went, at that position (the page never plays it again by itself)
// navTok: the latest navigation (Next, Previous, a song ending, a tap), navTarget: where a Next or Previous still on its
// way is going (several pressed fast count from there, not from the one still playing)
export const B = { active: false, queue: [], index: -1, source: null, meta: null, loop: 'no', error: false, leaving: false, parked: null, parkedPlaying: false, vol: Number(lsGet('bpVol') || 80), navTok: 0, navTarget: null };
el.volume = B.vol / 100;
// playing here with the page hidden (a locked phone): keep listening; paused and hidden: stop (battery)
el.addEventListener('play', () => {
	openSSE();
	// a Play from anywhere (the mini player, Space, the headset) tells the handheld, which pauses: one sound only
	const s = store.snap;
	if (B.meta && (!s || s.paused === false || !s.browserPlaying || s.path !== B.meta.p)) announce();
});
el.addEventListener('pause', () => { if (document.hidden) closeSSE(); });
// every browser-play and browser-pos carries this tab and a number that only grows (across reloads too): two posts can
// overtake each other on the way, and the handheld drops the older (it would open an older file after a newer one)
let wordSeq = Date.now() * 1000;
const word = o => Object.assign(o, { tab: TAB_ID, seq: ++wordSeq });
// where this page is in its file, for the handheld: its own Play resumes from here (not from its stale paused copy)
let posTimer = 0;
function sendPos() {
	if (!B.active || !B.meta || B.leaving || B.parked != null || store.conn !== 'ok' || !el.getAttribute('src')) return;
	post('/api/browser-pos', word({ p: B.meta.p, t: (el.currentTime || 0).toFixed(1), playing: el.paused ? 0 : 1 })).catch(() => {});
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
	post('/api/browser-play', word({ p })).catch(() => {});
}

export function probeOK(m) {
	if (m.playable === 'no') return false;
	if (m.playable === 'maybe' && m.mime) { const hint = m.mime + (m.codecs ? '; codecs="' + m.codecs + '"' : ''); return !!el.canPlayType(hint) || !!el.canPlayType(m.mime); }
	return true;
}
export async function probe1(p) {
	// not worth a long wait: unknown, the browser just tries the file
	try { return await get('/api/media', { p }, { ms: 4000, quiet: true }); } catch (e) { return null; }
}
export const fileUrl = p => '/api/file?p=' + encodeURIComponent(p);
// items: [{p, title?}]; plays the first one the browser can, skipping the rest it can't. auto: the next song after one
// ended (only while connected); everything else comes from a tap
// dir: the way to look for one it can play (Previous looks back); nav: a step along the list (Next, Previous, a song
// that ended), whose caller says when there's nothing more
// tok: the navigation it belongs to (browserNext's); anything else starts a new one. Only the latest gets to load:
// one overtaken while it looked into its file (a newer Next, a tap) gives up, saying null
export async function browserPlay(items, index, source, pos, auto, dir, nav, tok) {
	if (store.conn !== 'ok') return false;
	if (NO_HERE) { toast(NO_HERE_WHY + ': it plays on the handheld'); return false; }
	if (tok == null) { tok = ++B.navTok; B.navTarget = null; }
	dir = dir || 1;
	let skipped = 0;
	for (let i = index; i >= 0 && i < items.length; i += dir) {
		const it = items[i], m = await probe1(it.p);
		if (tok !== B.navTok) return null;
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
// steps: how many at once (the handheld's presses, summed); fromHandheld: its L, which never restarts the song (as
// there). Pressed fast, each counts from where the one before is going, and only the last one's file loads
export async function browserNext(auto, dir, steps, fromHandheld) {
	dir = dir || 1; steps = Math.max(1, steps || 1);
	const n = B.queue.length, pending = B.navTarget != null, from = pending ? B.navTarget : B.index;
	let i = from + dir * steps;
	if (auto && B.loop === 'one') { el.currentTime = 0; el.play(); return; }
	if (i >= n)
	{
		if (B.loop === 'all') i = n ? i % n : 0;
		// a song that ended: the list is over. Next pressed on the last one: it says so and the song plays on (as the
		// handheld's Next does); pressed several times from before it: the last one
		else if (auto) { el.pause(); toast('End of ' + (B.source && B.source.title || 'the list')); return; }
		else if (from >= n - 1) { toast('This is the last one'); return; }
		else i = n - 1;
	}
	if (i < 0) i = B.loop === 'all' && n ? ((i % n) + n) % n : 0;
	if (dir < 0 && !pending && !fromHandheld && el.currentTime > 3) { el.currentTime = 0; return; }
	const tok = ++B.navTok;
	B.navTarget = i;
	// (the ones it can't play are passed over; none left that way: the end, as on the last one)
	const ok = await browserPlay(B.queue, i, B.source, 0, auto, dir, true, tok);
	if (tok !== B.navTok || ok === null) return;	// a newer one took over
	B.navTarget = null;
	if (ok || store.conn !== 'ok') return;
	if (auto) { el.pause(); toast('End of ' + (B.source && B.source.title || 'the list')); }
	else toast(dir < 0 ? 'This is the first one' : 'This is the last one');
}
function unload() { el.pause(); el.removeAttribute('src'); clearSubs(); el.load(); }
// play pressed on the handheld while this page played: the sound is the handheld's now, and so is the switch (else it
// still says "In this browser" over a page that's silent)
export function handheldTookOver() {
	browserStop();
	if (store.target !== 'handheld') { chooseTarget('handheld'); toast('Playing on your handheld now'); }
}
export function browserStop() {
	B.leaving = true; unload();
	Object.assign(B, { active: false, meta: null, queue: [], index: -1, error: false, parked: null, parkedPlaying: false, navTok: B.navTok + 1, navTarget: null });
	post('/api/browser-play', word({ stop: 1 })).catch(() => {});
	notify();
}
// forgets this page's file without telling the handheld (signed out, or the player started again)
export function dropBrowser() {
	B.leaving = true; unload();
	Object.assign(B, { active: false, meta: null, queue: [], index: -1, error: false, parked: null, parkedPlaying: false, navTok: B.navTok + 1, navTarget: null });
	notify();
}
// the connection went: stop at once (the stream can't go on), keep what and where
export function parkHere() {
	if (!B.meta || B.parked != null) return;
	const playing = !el.paused || (store.conn === 'blip' && B.wasPlaying);
	B.parked = bPos(); B.parkedPlaying = playing;
	el.pause(); el.removeAttribute('src'); el.load();
	notify();
}
export function playParked() {
	if (!B.meta || B.parked == null) return;
	const pos = B.parked;
	Object.assign(B, { parked: null, parkedPlaying: false, error: false });
	el.src = fileUrl(B.meta.p);
	restoreSubs();
	if (pos > 0) el.addEventListener('loadedmetadata', () => { el.currentTime = pos; }, { once: true });
	el.play().catch(() => {});
	notify();
}
export function setBrowserVol(v) { B.vol = v; el.volume = v / 100; lsSet('bpVol', v); notify(); }
function bPos() { return B.parked != null ? B.parked : el.currentTime || 0; }
function bDur() { return isFinite(el.duration) && el.duration ? el.duration : (B.meta && B.meta.duration) || 0; }

/* ---- subtitles in this browser: one <track> per text track, fetched as WebVTT when chosen ---- */
export const SUBS = { file: '', list: [], chosen: -1, urls: {}, progress: null, failed: {}, seq: 0 };
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
export async function chooseSub(i) {
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
export const CUE_SIZES = [['s', 'Small', .8], ['m', 'Medium', 1], ['l', 'Large', 1.25], ['xl', 'Extra large', 1.5]];
export const cueStyle = { size: lsGet('bpCueSize') || 'm', pos: lsGet('bpCuePos') || 'bottom' };
function placeCues() {
	for (const t of el.textTracks) {
		if (t.mode !== 'showing' || !t.cues) continue;
		for (const c of t.cues) { c.snapToLines = true; c.line = cueStyle.pos === 'raised' ? -4 : -2; }
	}
}
export function sizeCues() {
	const f = (CUE_SIZES.find(x => x[0] === cueStyle.size) || CUE_SIZES[1])[2];
	el.style.setProperty('--cue', Math.max(12, el.clientHeight * .045 * f).toFixed(1) + 'px');
}
if ('ResizeObserver' in window) new ResizeObserver(sizeCues).observe(el);
export function setCueStyle(k, v) { cueStyle[k] = v; lsSet(k === 'size' ? 'bpCueSize' : 'bpCuePos', v); sizeCues(); placeCues(); notify(); }

export function updateMediaSession() {
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
export function view() {
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
export const hereNow = () => store.target === 'browser' && B.meta;
export const ctl = {
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
export async function setTarget(t) {
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
export async function continueHere() {
	const s = store.snap; if (!s || !s.path || !userTap()) return;
	const pos = s.paused ? s.position : s.position + (Date.now() - store.snapAt) / 1000 * (s.speed || 1);
	const src = s.source ? { kind: s.source.kind, title: s.source.title, id: s.source.id } : null;
	// the file at once; the rest of its list when the handheld has said it (it can take a moment while it switches)
	const ok = await browserPlay([{ p: s.path }], 0, src, pos);
	if (ok === null) return;	// something else was picked meanwhile
	if (!ok) { chooseTarget('handheld'); return; }
	chooseTarget('browser'); toast('Continuing here from ' + fmtTime(pos));
	get('/api/source').then(q => {
		const i = q.items ? q.items.findIndex(x => x.p === s.path) : -1;
		if (i >= 0 && B.meta && B.meta.p === s.path && B.queue.length === 1) { B.queue = q.items; B.index = i; notify(); }
	}).catch(() => {});
}
// aid: the audio track to play there (the browser can only play the first)
export async function handoffToHandheld(aid) {
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
export async function playPaths(paths, folderSet, target, stay) {
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
export async function playPlaylist(id, name, items, index, shuffle, target, tapped, stay) {
	if (!tapped && !userTap()) return;
	target = target || store.target;
	if (target === 'browser') {
		let list = items.filter(i => !i.missing && i.p).map(i => ({ p: i.p, title: i.title }));
		if (shuffle) list = list.sort(() => Math.random() - .5);
		const idx = shuffle ? 0 : Math.max(0, list.findIndex(i => i.p === items[index || 0].p));
		if (await browserPlay(list, idx, { kind: 'playlist', title: name, id }, 0)) { markUsed(id); chooseTarget('browser'); if (!stay) go('#/playing'); return true; }
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
export async function playHere(play, p, playable) {
	if (store.target === 'browser') {
		const m = playable === 'no' ? { playable: 'no' } : await probe1(p);
		if (m && !probeOK(m)) { toast('Not supported in this browser · play it on the handheld instead', { label: 'Play on handheld', fn: () => play('handheld') }, 8000); return; }
	}
	await play(store.target);
}

