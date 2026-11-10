import { useState, useLayoutEffect, useRef } from '../vendor/htm-preact-standalone.module.js';

// this tab's own id, kept across its reloads (sessionStorage is per tab): the server swaps this tab's old live stream
// for the new one at once
// this tab's id, kept across its reloads (sessionStorage). A duplicated tab copies sessionStorage, id and all, while
// the original is still open: 'bpTabLive' says so (set while the page shows, cleared as it goes away; a reload clears
// it first), and the copy takes an id of its own, or the two would act as one output
export const TAB_ID = (() => {
	const fresh = () => Math.random().toString(36).slice(2, 10);
	try {
		let t = sessionStorage.getItem('bpTab');
		if (!t || sessionStorage.getItem('bpTabLive') === '1') { t = fresh(); sessionStorage.setItem('bpTab', t); }
		const live = v => { try { sessionStorage.setItem('bpTabLive', v); } catch (e) {} };
		live('1');
		addEventListener('pagehide', () => live('0'));
		addEventListener('pageshow', () => live('1'));
		return t;
	} catch (e) { return fresh(); }
})();

/* ================= store ================= */
// Playing in the page was off on iPhone and iPad (Safari's history and audio session broke it once); on again for
// testing there. true turns it off everywhere (the handheld plays, the page is the remote)
export const NO_HERE = false;
export const NO_HERE_WHY = 'Not on iPhone or iPad yet';
// the layouts (as style.css): a phone (also one on its side: short and touch), a tablet (700 px up), a computer (1100 up)
export const Q_WIDE = '(min-width: 700px) and (min-height: 521px), (min-width: 700px) and (hover: hover)';
export const Q_DESK = '(min-width: 1100px) and (min-height: 521px), (min-width: 1100px) and (hover: hover)';
export const phoneNow = () => !matchMedia(Q_WIDE).matches;
export const store = {
	// authed: null till the first answer; signedOut: why the sign-in page shows ('pin', 'expired', '')
	authed: null, signedOut: '', epoch: 0,
	// conn: 'ok', 'blip' (failing, the app stays; reconnecting: its banner, after 2 s), 'down' (the down screen)
	conn: 'ok', reconnecting: false, downReason: '', probing: false, nextTry: 0,
	snap: null, snapAt: 0, eq: null,
	// where picks go: 'browser' when the Session's pref is this tab (browser-player.js onSession), else 'handheld'
	target: 'handheld',
	toasts: [], modals: [], lightbox: null, pick: null, sel: new Set(), upload: { items: [], paused: false }, pls: null, upDone: 0,
	help: false, dropping: false, fsTick: 0, plTick: 0, route: parseRoute(), fresh: {}, media: null,
};
const listeners = new Set();
let version = 0;
export function set(patch) { Object.assign(store, patch); version++; listeners.forEach(f => f()); }
export function notify() { set({}); }
// subscribes before paint, and catches up with a change made between the render and the subscription (boot can
// answer in that gap: the page stayed on "Connecting…")
export function useStore() {
	const [, n] = useState(0), seen = useRef(version);
	seen.current = version;
	useLayoutEffect(() => { const f = () => n(x => x + 1); listeners.add(f); if (version !== seen.current) f(); return () => listeners.delete(f); }, []);
	return store;
}
export function lsGet(k) { try { return localStorage.getItem(k); } catch (e) { return null; } }
export function lsSet(k, v) { try { localStorage.setItem(k, v); } catch (e) {} }

/* ================= routes: #/playing, #/files/<vpath>, #/playlists[/<id>], #/recent (the history bookkeeping is in router.js) ================= */
export function parseRoute() {
	const h = location.hash.replace(/^#\/?/, '');
	const [a, ...rest] = h.split('/');
	const tail = rest.map(decodeURIComponent).join('/');
	if (a === 'files') return { page: 'files', path: tail };
	if (a === 'playlists') return { page: 'playlists', id: tail };
	if (a === 'recent') return { page: 'playlists', id: '~recent' };	// (recent.js's RECENT_ID: Recent sits with the playlists)
	return { page: 'playing' };
}
