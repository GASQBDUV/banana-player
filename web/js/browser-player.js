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
	// a Play from anywhere (the mini player, Space, the headset) tells the handheld, which pauses: one sound only. Only
	// when the handheld has something to silence: after its Stop it holds nothing, and this page plays on alone (the
	// first "Play in this browser" opens the held copy through its own browser-play)
	const s = store.snap;
	if (B.meta && (!s || (s.path && (s.paused === false || !s.browserPlaying || s.path !== B.meta.p)))) announce();
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
	post('/api/browser-pos', word({ p: B.meta.p, t: bPos().toFixed(1), playing: el.paused ? 0 : 1 })).catch(() => {});
}
for (const ev of ['play', 'pause', 'seeked']) el.addEventListener(ev, () => {
	sendPos();
	clearInterval(posTimer); posTimer = 0;
	if (!el.paused) posTimer = setInterval(() => { if (el.paused || !B.active) { clearInterval(posTimer); posTimer = 0; } else sendPos(); }, 10000);
});
el.addEventListener('emptied', () => { clearInterval(posTimer); posTimer = 0; });
for (const ev of ['play', 'pause', 'loadedmetadata', 'durationchange', 'waiting', 'playing', 'seeked']) el.addEventListener(ev, () => { updateMediaSession(); notify(); });
el.addEventListener('ended', () => {
	// a Direct Stream cut short (the connection dropped while paused, say): on from where it stopped
	if (B.remux && store.conn === 'ok' && bDur() && bPos() < bDur() - 2) { remuxAt(B.remux.a, bPos(), true); return; }
	if (store.conn === 'ok') browserNext(true); else notify();
});
el.addEventListener('error', () => {
	if (!B.active || !el.getAttribute('src')) return;
	// the Direct Stream failed: back to the file as it is (its default sound)
	if (B.remux) { remuxFailed(el.error); return; }
	B.error = true; notify(); const m = B.meta; toast("This browser can't play this file", m && { label: 'Play on handheld', fn: () => handoffToHandheld() }, 8000); });
function announce() {
	const p = B.meta.p;
	if (B.announced === p && Date.now() - B.announcedAt < 1500) return;
	B.announced = p; B.announcedAt = Date.now(); B.handheldQuiet = false;
	// (open: the handheld's pick this plays, so the handheld can tell an answer to an older pick from a newer one)
	post('/api/browser-play', word(B.openTok ? { p, open: B.openTok } : { p })).catch(() => {});
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
	if (tok == null) { tok = ++B.navTok; B.navTarget = null; B.openTok = 0; }
	dir = dir || 1;
	let skipped = 0;
	for (let i = index; i >= 0 && i < items.length; i += dir) {
		// (m: looked into already, by handheldOpen)
		const it = items[i], m = it.m !== undefined ? it.m : await probe1(it.p);
		delete it.m;
		if (tok !== B.navTok) return null;
		if (m && !probeOK(m)) { skipped++; continue; }
		if (auto && store.conn !== 'ok') return false;
		const kind = kindOfName(it.p) === 'video' || (m && m.vcodec) ? 'video' : 'audio';
		Object.assign(B, { shuffled: items === B.queue && B.shuffled, active: true, queue: items, index: i, source, error: false, leaving: false, parked: null, parkedPlaying: false,
			meta: { p: it.p, title: titleFor(it.p, m && m.title), artist: m && m.artist || '', album: m && m.album || '', kind, chapters: m && m.chapters || [], duration: m && m.duration || 0, media: m } });
		B.remux = null; clearTimeout(remuxTimer);
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
// steps: how many at once (the handheld's presses, summed); fromHandheld: its L. Pressed fast, each counts from where
// the one before is going, and only the last one's file loads
const RESTART_S = 5;
export async function browserNext(auto, dir, steps, fromHandheld) {
	dir = dir || 1; steps = Math.max(1, steps || 1);
	const n = B.queue.length, pending = B.navTarget != null, from = pending ? B.navTarget : B.index;
	// Previous as YouTube Music's (the handheld's too): RESTART_S or more in, back to the start; a burst of them from
	// there (the handheld's n) is that and then the ones before. Before RESTART_S, the one before
	if (dir < 0 && !auto && !pending && bPos() >= RESTART_S) {
		if (steps === 1) { seekTo(0); return; }
		steps--;
	}
	let i = from + dir * steps;
	if (auto && B.loop === 'one') { seekTo(0); el.play().catch(() => {}); return; }
	if (i >= n)
	{
		if (B.loop === 'all') i = n ? i % n : 0;
		// a song that ended: the list is over. Next pressed on the last one: it says so and the song plays on (as the
		// handheld's Next does); pressed several times from before it: the last one
		else if (auto) { el.pause(); toast('End of ' + (B.source && B.source.title || 'the list')); return; }
		else if (from >= n - 1) { toast('This is the last one'); return; }
		else i = n - 1;
	}
	if (i < 0) {
		if (B.loop === 'all' && n) i = ((i % n) + n) % n;
		// on the first one, under RESTART_S in: nothing (as on the handheld)
		else if (from <= 0) { toast('This is the first one'); return; }
		else i = 0;
	}
	const tok = ++B.navTok;
	B.navTarget = i; B.openTok = 0;
	// (the ones it can't play are passed over; none left that way: the end, as on the last one)
	const ok = await browserPlay(B.queue, i, B.source, 0, auto, dir, true, tok);
	if (tok !== B.navTok || ok === null) return;	// a newer one took over
	B.navTarget = null;
	if (ok || store.conn !== 'ok') return;
	if (auto) { el.pause(); toast('End of ' + (B.source && B.source.title || 'the list')); }
	else toast(dir < 0 ? 'This is the first one' : 'This is the last one');
}
function unload() { el.pause(); B.remux = null; clearTimeout(remuxTimer); el.removeAttribute('src'); clearSubs(); el.load(); }
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
	restoreSubs();
	if (B.remux) { remuxAt(B.remux.a, pos, true); return; }
	el.src = fileUrl(B.meta.p);
	if (pos > 0) el.addEventListener('loadedmetadata', () => { el.currentTime = pos; }, { once: true });
	el.play().catch(() => {});
	notify();
}
// A pressed on the handheld while this page plays: play or pause here, as asked (the state, not a flip: a press the
// handheld sends twice can't undo itself). Already so, or the browser refusing to play: it hears where this page is
export function handheldToggle(play) {
	if (!B.active || !B.meta || B.leaving) return;
	if (B.parked != null) { if (play && store.conn === 'ok') playParked(); return; }
	if (play && el.paused) el.play().catch(() => sendPos());
	else if (!play && !el.paused) el.pause();
	else sendPos();
}
// a song or video picked on the handheld while this page is the sound ({tok, tab, p, list, index, source, pos}): it
// plays here, in the handheld's list, and the handheld holds it paused (browser-play). One this browser can't play
// (the probe says so) is refused, and the handheld plays it. Picked fast, only the latest loads (navTok)
export async function handheldOpen(d) {
	if (d.tab && d.tab !== TAB_ID) return;
	const refuse = why => { console.log('handheld-open refused: ' + why); post('/api/browser-refuse', { tok: d.tok }).catch(() => {}); };
	if (!B.active || B.leaving || B.parked != null || NO_HERE || store.conn !== 'ok' || !Array.isArray(d.list) || !d.list[d.index]) return refuse('not playing here');
	const tok = ++B.navTok; B.navTarget = null; B.openTok = d.tok;
	const items = d.list.map(p => ({ p })), first = items[d.index];
	first.m = await probe1(first.p);
	if (tok !== B.navTok) return;
	if (first.m && !probeOK(first.m)) return refuse('format');
	const src = d.source && d.source.kind ? { kind: d.source.kind, title: d.source.title, id: d.source.id } : null;
	const ok = await browserPlay(items, d.index, src, d.pos > 0 ? d.pos : 0, false, 1, true, tok);
	if (ok === false) refuse('failed');
	else if (ok && store.target !== 'browser') chooseTarget('browser');
}
export function setBrowserVol(v) { B.vol = v; el.volume = v / 100; lsSet('bpVol', v); notify(); }
export function bPos() {
	if (B.parked != null) return B.parked;
	if (B.remux) return B.remux.pending != null ? B.remux.pending : B.remux.base + (el.currentTime || 0);
	return el.currentTime || 0;
}
// (a Direct Stream doesn't know its own length: the file's)
function bDur() { return !B.remux && isFinite(el.duration) && el.duration ? el.duration : (B.meta && B.meta.duration) || (isFinite(el.duration) && el.duration) || 0; }
// to a place in the file, whichever way it plays: a Direct Stream can't seek, so it starts again from there
export function seekTo(t) {
	if (B.remux) {
		// (a drag along the bar asks once, where it stops)
		const r = B.remux;
		if (r.pending == null) r.play = !el.paused;
		r.pending = t; notify();
		clearTimeout(remuxTimer); remuxTimer = setTimeout(() => remuxAt(r.a, t, r.play), 250);
		return;
	}
	el.currentTime = t;
}

/* ---- audio tracks: Safari switches them in the element (audioTracks); elsewhere the server's Direct Stream copies
   the video and the chosen track into a fragmented MP4 from a time on (/api/remux), and the original file is back for
   the default track. The page counts the stream's time from where it starts (B.remux.base) ---- */
let remuxTimer = 0;
const VC = { h264: 'avc1.640028', hevc: 'hvc1.1.6.L120.90', vp9: 'vp09.00.40.08', av1: 'av01.0.08M.08' };
const AC = { aac: 'mp4a.40.2', mp3: 'mp4a.69', opus: 'opus', flac: 'flac', ac3: 'ac-3', eac3: 'ec-3' };
const typeOk = (v, a) => { try { return !!el.canPlayType('video/mp4; codecs="' + v + ', ' + a + '"') || !!(window.MediaSource && MediaSource.isTypeSupported('video/mp4; codecs="' + v + ', ' + a + '"')); } catch (e) { return false; } };
// the track the browser plays from the file itself (Direct Play)
export const directAudio = m => Math.max(0, (m && m.audio || []).findIndex(a => a.default));
// '' when audio track i can play here by Direct Stream, else why not
export function streamWhy(m, i) {
	const a = m && m.audio && m.audio[i];
	if (!a) return 'No such track';
	if (B.noStream && B.meta && B.meta.media === m && B.noStream === B.meta.p && i !== directAudio(m)) return "This browser can't play this video's format in a new stream";
	if (!VC[m.vcodec] || !typeOk(VC[m.vcodec], 'mp4a.40.2') && !typeOk(VC[m.vcodec], 'opus')) return "This video can't be streamed to this browser";
	if (!AC[a.codec] || !typeOk(VC[m.vcodec], AC[a.codec])) return 'Plays on the handheld only';
	return '';
}
// Safari: the file's own tracks, switched in place
export const nativeAudio = () => el.audioTracks && el.audioTracks.length > 1;
export function playingAudio() {
	const m = B.meta && B.meta.media;
	if (nativeAudio()) return [...el.audioTracks].findIndex(t => t.enabled);
	return B.remux ? B.remux.a : directAudio(m);
}
// the chosen track: in place (Safari), the file itself (its default track), else a Direct Stream from here
export async function chooseAudio(i) {
	const m = B.meta && B.meta.media; if (!m || B.parked != null) return;
	if (nativeAudio()) { [...el.audioTracks].forEach((t, j) => t.enabled = j === i); notify(); return; }
	if (i === playingAudio() && !B.switching) return;
	if (i === directAudio(m)) { directPlay(bPos(), !el.paused); return; }
	await remuxAt(i, bPos(), !el.paused);
}
// the Direct Stream didn't play: back to the file as it is (its default sound), saying why, and the handheld's log
// hears what the browser said. A format it can't decode is remembered for this file (the other tracks say so)
function remuxFailed(err) {
	const pos = bPos(), r = B.remux, code = err ? err.code : 0;
	const format = code === 3 || code === 4;
	const why = format ? "This browser can't play this video's format in a new stream" : 'The stream from your handheld broke off';
	if (format && B.meta) B.noStream = B.meta.p;
	post('/api/remux-failed', { p: B.meta ? B.meta.p : '', a: r ? r.a : '', t: pos.toFixed(1), why: 'MediaError ' + code + ' ' + (err && err.message || ''), ua: navigator.userAgent }).catch(() => {});
	toast("Couldn't switch the audio track: " + why.charAt(0).toLowerCase() + why.slice(1), null, 8000);
	directPlay(pos, true);
}
function directPlay(pos, play) {
	B.remux = null; B.switching = null; clearTimeout(remuxTimer);
	el.src = fileUrl(B.meta.p);
	if (pos > 0) el.addEventListener('loadedmetadata', () => { el.currentTime = pos; }, { once: true });
	if (play) el.play().catch(() => {});
	shiftSubs();
	notify();
}
async function remuxAt(a, t, play) {
	const p = B.meta && B.meta.p, tok = B.navTok;
	if (!p) return;
	B.switching = { a }; notify();
	let r;
	try { r = await get('/api/remux-start', { p, a, t: t.toFixed(2) }, { ms: 15000, quiet: true }); }
	catch (e) { if (B.meta && B.meta.p === p && tok === B.navTok) { B.switching = null; if (B.remux) B.remux.pending = null; notify(); toast(e.status === 0 ? "Couldn't switch the audio track" : errMsg(e)); } return; }
	if (!B.meta || B.meta.p !== p || tok !== B.navTok || (B.switching && B.switching.a !== a)) return;	// moved on meanwhile
	B.remux = { a, base: r.start, pending: null };
	B.switching = null;
	el.src = '/api/remux?' + new URLSearchParams({ p, a, t: r.start.toFixed(3), tab: TAB_ID });
	if (play) el.play().catch(() => {});
	shiftSubs();
	notify();
}

/* ---- subtitles in this browser: one <track> per text track, fetched as WebVTT when chosen ---- */
export const SUBS = { file: '', list: [], chosen: -1, urls: {}, progress: null, failed: {}, seq: 0, noAss: {}, ass: false };
function clearSubs() {
	assOff();
	[...el.querySelectorAll('track')].forEach(t => t.remove());
	Object.values(SUBS.urls).forEach(u => URL.revokeObjectURL(u));
	Object.assign(SUBS, { file: '', list: [], chosen: -1, last: -1, urls: {}, progress: null, failed: {}, seq: SUBS.seq + 1, noAss: {}, ass: false });
}
function subsForFile(m) {
	clearSubs();
	SUBS.file = B.meta ? B.meta.p : '';
	SUBS.list = m && m.subs || [];
	const want = lsGet('bpSubOff') === '1' ? -1 : m && m.subDefault != null ? m.subDefault : -1;
	const sub = SUBS.list.find(x => x.i === want);
	if (sub && sub.kind !== 'image') chooseSub(want);
}
// the subtitles last shown (their label), for CC: the same ones again, in this file or the next
const SIGNS = /sign|song|karaoke|forced/i;
function rememberSub(i) { const x = SUBS.list.find(y => y.i === i); if (x) { SUBS.last = i; lsSet('bpSubLast', x.label); } }
// CC on the video (and the C key): off, or back on with the last ones; in a new file the same label, else the same
// language, else the file's default, else the first that isn't signs only
export function toggleCaptions() {
	if (SUBS.chosen >= 0) { lsSet('bpSubOff', '1'); chooseSub(-1); return; }
	const text = SUBS.list.filter(x => x.kind !== 'image');
	if (!text.length) { toast(SUBS.list.length ? "This video's subtitles show on the handheld only" : 'No subtitles in this file'); return; }
	const label = lsGet('bpSubLast') || '', lang = label.split(' · ')[0];
	const m = B.meta && B.meta.media;
	const pick = text.find(x => x.i === SUBS.last) || text.find(x => x.label === label)
		|| text.find(x => lang && x.label.split(' · ')[0] === lang && !SIGNS.test(x.label)) || text.find(x => m && x.i === m.subDefault)
		|| text.find(x => !SIGNS.test(x.label)) || text[0];
	lsSet('bpSubOff', '0'); chooseSub(pick.i);
}
// playback speed (the gear's Speed): it stays for the next file, as on YouTube
export const SPEEDS = [0.5, 0.75, 1, 1.25, 1.5, 2];
export function setSpeed(r) { el.defaultPlaybackRate = r; el.playbackRate = r; notify(); }
function restoreSubs() { const c = SUBS.chosen; if (c >= 0) { SUBS.chosen = -1; chooseSub(c); } }
export async function chooseSub(i) {
	const file = SUBS.file, seq = SUBS.seq;
	SUBS.chosen = i; SUBS.progress = null; delete SUBS.failed[i];
	if (i >= 0) rememberSub(i);
	[...el.textTracks].forEach(t => t.mode = 'disabled');
	assOff();
	notify();
	if (i < 0) return;
	const sx = SUBS.list.find(x => x.i === i);
	// styled (ASS/SSA): drawn by libass in the page, in its own look; plain cues if that can't run here
	if (sx && sx.styled && !SUBS.noAss[i] && assCan()) { if (await chooseAss(i, file, seq)) return; if (SUBS.seq !== seq || SUBS.chosen !== i) return; }
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
/* ---- styled subtitles (ASS/SSA) by JASSUB (libass in WebAssembly, web/vendor/jassub/), loaded the first time one is
   chosen. It draws on a canvas over the picture (assBox, which goes where the video goes), with the file's own fonts
   (its Matroska attachments) and Inter for what they lack ---- */
export const assBox = document.createElement('div');
assBox.className = 'assbox';
document.getElementById('holder').appendChild(assBox);
const ASS = { jas: null, mod: null };
const abs = u => new URL(u, location.href).href;
const assCan = () => typeof WebAssembly === 'object' && typeof Worker === 'function';
function assOff() {
	if (ASS.jas) { try { ASS.jas.destroy(); } catch (e) {} ASS.jas = null; }
	assBox.textContent = '';
	SUBS.ass = false;
}
// true: it shows; false: use the plain cues instead
async function chooseAss(i, file, seq) {
	const live = () => SUBS.seq === seq && SUBS.chosen === i;
	let text = SUBS.assText && SUBS.assText[i];
	if (text == null) {
		for (;;) {
			let r, t;
			try { ({ r, text: t } = await fetchT('/api/ass?' + new URLSearchParams({ p: file, i }), {}, 60000)); } catch (e) { r = null; }
			if (!live()) return true;
			if (r && r.status === 202) {
				let pr = 0; try { pr = JSON.parse(t).progress || 0; } catch (e) {}
				SUBS.progress = pr; notify();
				await new Promise(res => setTimeout(res, 1000));
				if (!live()) return true;
				continue;
			}
			SUBS.progress = null;
			if (!r || !r.ok) { SUBS.noAss[i] = 1; return false; }
			text = t; (SUBS.assText = SUBS.assText || {})[i] = t;
			break;
		}
	}
	let fonts = [];
	try { const f = await get('/api/fonts', { p: file }, { ms: 8000, quiet: true }); fonts = (f.fonts || []).map(x => abs('/api/font?' + new URLSearchParams({ p: file, i: x.i }))); } catch (e) {}
	if (!live()) return true;
	try {
		if (!ASS.mod) ASS.mod = (await import('../vendor/jassub/jassub.es.js')).default;
		if (!live()) return true;
		assOff();
		const canvas = document.createElement('canvas');
		assBox.appendChild(canvas);
		ASS.jas = new ASS.mod({ video: el, canvas, subContent: text, fonts,
			workerUrl: abs('/vendor/jassub/jassub-worker.js'), wasmUrl: abs('/vendor/jassub/jassub-worker.wasm'),
			availableFonts: { inter: abs('/fonts/Inter-Regular.ttf') }, fallbackFont: 'inter', timeOffset: B.remux ? B.remux.base : 0, libassMemoryLimit: 48, libassGlyphLimit: 24 });
		ASS.jas.addEventListener('error', () => { if (!live() || !SUBS.ass) return; console.log('jassub failed: plain cues'); SUBS.noAss[i] = 1; chooseSub(i); });
		SUBS.ass = true;
		notify();
		return true;
	} catch (e) {
		console.log('jassub: ' + e);
		assOff(); SUBS.noAss[i] = 1;
		return false;
	}
}

// the viewer's own look: size (a share of the picture's height) and how high the lines sit
export const CUE_SIZES = [['s', 'Small', .8], ['m', 'Medium', 1], ['l', 'Large', 1.25], ['xl', 'Extra large', 1.5]];
export const cueStyle = { size: lsGet('bpCueSize') || 'm', pos: lsGet('bpCuePos') || 'bottom' };
function placeCues() {
	const off = B.remux ? B.remux.base : 0;
	for (const t of el.textTracks) {
		if (t.mode !== 'showing' || !t.cues) continue;
		for (const c of t.cues) {
			c.snapToLines = true; c.line = cueStyle.pos === 'raised' ? -4 : -2;
			// a Direct Stream's clock starts at its base: the file's times, moved back by it
			if (c.bpS == null) { c.bpS = c.startTime; c.bpE = c.endTime; }
			if (c.bpOff !== off) { c.bpOff = off; c.startTime = c.bpS - off; c.endTime = c.bpE - off; }
		}
	}
}
function shiftSubs() { placeCues(); if (ASS.jas) ASS.jas.timeOffset = B.remux ? B.remux.base : 0; }
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
	seek(t) { if (hereNow()) { if (B.parked != null) { B.parked = t; notify(); } else seekTo(t); } else { cmd('seek', { t: t.toFixed(1) }); if (store.snap) { store.snap.position = t; store.snapAt = Date.now(); notify(); } } },
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
// stay: a row's tap plays and the page stays (the small player shows it) — for a song; a video opens the player,
// where it's watched (sv: stay for this file?)
const sv = (stay, p) => stay && kindOfName(p || '') !== 'video';
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
		if (!sv(stay, items[idx] && items[idx].p)) go('#/playing');
		return true;
	} else {
		const was = B.active && !el.paused; el.pause();
		try { await post('/api/play', paths.length === 1 ? { p: paths[0] } : { p: paths }); }
		catch (e) { if (was && store.conn === 'ok') el.play().catch(() => {}); toast(e.status === 0 ? "Couldn't start it on your handheld" : errMsg(e)); return false; }
		if (B.active) browserStop();
		chooseTarget('handheld');
		if (!sv(stay, paths.length === 1 && !folderSet.has(paths[0]) ? paths[0] : '')) go('#/playing');
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
		if (await browserPlay(list, idx, { kind: 'playlist', title: name, id }, 0)) { markUsed(id); chooseTarget('browser'); if (!sv(stay, list[idx] && list[idx].p)) go('#/playing'); return true; }
		return false;
	} else {
		const was = B.active && !el.paused; el.pause();
		try { await post('/api/play', { playlist: id, index: index || 0, shuffle: shuffle ? 1 : 0 }); }
		catch (e) { if (was && store.conn === 'ok') el.play().catch(() => {}); toast(e.status === 0 ? "Couldn't start it on your handheld" : errMsg(e)); return false; }
		if (B.active) browserStop();
		chooseTarget('handheld');
		if (!sv(stay, !shuffle && items[index || 0] ? items[index || 0].p : '')) go('#/playing');
		return true;
	}
}
// one file from where it was left (Recent): the handheld finds its own place (/api/play), this browser starts at pos
export async function playAt(p, pos, target, stay) {
	target = target || store.target;
	if (target === 'browser') {
		if (!await browserPlay([{ p }], 0, { kind: 'playlist', title: 'Recent' }, pos > 0 ? pos : 0)) return false;
		chooseTarget('browser');
		if (!sv(stay, p)) go('#/playing');
		return true;
	}
	const was = B.active && !el.paused; el.pause();
	try { await post('/api/play', { p }); }
	catch (e) { if (was && store.conn === 'ok') el.play().catch(() => {}); toast(e.status === 0 ? "Couldn't start it on your handheld" : errMsg(e)); return false; }
	if (B.active) browserStop();
	chooseTarget('handheld');
	if (!sv(stay, p)) go('#/playing');
	return true;
}
// a row's tap: it plays at once where the last choice was; one this browser can't play goes to the handheld (said so)
export async function playHere(play, p, playable) {
	if (store.target === 'browser') {
		const m = playable === 'no' ? { playable: 'no' } : await probe1(p);
		if (m && !probeOK(m)) { toast('Not supported in this browser · play it on the handheld instead', { label: 'Play on handheld', fn: () => play('handheld') }, 8000); return; }
	}
	await play(store.target);
}

