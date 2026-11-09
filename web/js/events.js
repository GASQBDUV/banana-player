import { TAB_ID, set, store } from './store.js';
import { B, browserNext, el, handheldTookOver, updateMediaSession } from './browser-player.js';
import { markUsed } from './plshared.js';
import { C, connFail, goDown, signOutHere } from './connection.js';
import { markTrack } from './toasts.js';
import { fetchT, get } from './api.js';

/* ================= live updates ================= */
let es = null;
export function applySnap(s) {
	const prev = store.snap;
	set({ snap: s, snapAt: Date.now() });
	// the handheld plays again (paused -> playing): this page pauses its player. Not on a snapshot that merely still
	// says "playing": Continue here starts the browser a moment before the handheld's pause shows in the state
	// (and only once it has been seen paused on this page's file: opening it there, it can look like playing a moment)
	if (B.active && B.meta && s.path === B.meta.p && s.paused && !s.loading) B.handheldQuiet = true;
	if (s.paused === false && !s.loading && prev && prev.paused === true && B.active && B.handheldQuiet && !el.paused && !B.leaving && Date.now() - (B.announcedAt || 0) > 2000) handheldTookOver();
	if (!prev || prev.playlistSerial !== s.playlistSerial) set({ plTick: store.plTick + 1 });
	// a playlist playing on the handheld is a recent one (the Playlists page's Recent, the sidebar's)
	if (s.source && s.source.kind === 'playlist' && s.source.id && !s.paused && (!prev || prev.path !== s.path)) markUsed(s.source.id);
	updateMediaSession();
}
export function openSSE() {
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
	// Prev / Next pressed on the handheld while this page plays: this page's list moves on (as its own buttons do), by
	// as many as were pressed (n); pressed fast, browserNext sums them and loads only the last
	on('handheld-skip', d => { if (B.active && B.meta && B.parked == null && (d.dir === 1 || d.dir === -1)) { markTrack(d.dir); browserNext(false, d.dir, d.n || 1, true); } });
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
export function closeSSE() { if (es) { es.close(); es = null; } clearInterval(C.wd); C.wd = 0; }
export function playingHere() { return B.active && !el.paused; }
// this browser's video shows in the full player (the media element sits in its slot)
export const videoHere = () => store.target === 'browser' && !!B.meta && B.meta.kind === 'video';
export async function refreshState() { try { applySnap(await get('/api/state')); } catch (e) {} }

