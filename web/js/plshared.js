import { html, useState, useEffect, useRef } from '../vendor/htm-preact-standalone.module.js';
import { errMsg, errToast, get, post } from './api.js';
import { filesHash, go } from './router.js';
import { Modal, PlArt } from './components.js';
import { openModal, toast } from './toasts.js';
import { lsGet, lsSet, notify, set, store, useStore } from './store.js';
import { compactNow, reduceMotion } from './playing.js';
import { Icon } from './icons.js';
import { cmpName, dirName, kindOfName, plural, titleFor, touchOnly } from './util.js';
import { markFresh } from './uploads.js';

/* ================= playlist edits shared by Files and Playlists ================= */
export async function plEdit(id, act, params, rev) {
	if (rev == null) rev = (await get('/api/playlist', { id })).rev;
	return post('/api/playlist/' + act, Object.assign({ id, rev }, params));
}
export const validPlName = (n, others, self) => {
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
export const nameDialog = props => openModal(close => html`<${NameDialog} ...${props} onClose=${close} />`);

export function newPlaylistDialog(onCreated) {
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
export const LIKED_ID = '.liked', LIKED_MAX = 2000;
let likesSeq = 0;
export async function loadLikes() {
	const n = ++likesSeq;
	try { const r = await get('/api/likes', null, { quiet: true }); if (n === likesSeq) set({ likes: new Set(r.paths || []), likesCount: r.count || 0 }); } catch (e) {}
}
// what a heart shows: a press on its way first, then the state's flag for the handheld's file, then the list
export function isLiked(p) {
	if (!p) return false;
	const h = store.likeHeld[p];
	if (h && Date.now() < h.until) return h.v;
	const s = store.snap;
	if (s && s.path === p && typeof s.liked === 'boolean') return s.liked;
	return !!(store.likes && store.likes.has(p));
}
export const likable = v => !!(v && v.has && v.path && (v.kind === 'audio' || v.kind === 'video'));
// key: the file's virtual path (what the hearts look up); p: what the server is asked about, the same path, or
// "current" for the handheld's file. how 'menu': a menu's Like says so, with Undo (a heart is its own answer)
export async function setLike(key, on, how, p) {
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
export function LikeBtn({ v, cls }) {
	const on = isLiked(v.path), [pop, setPop] = useState(false), seen = useRef({ p: v.path, on }), t = useRef(0);
	useEffect(() => {
		const was = seen.current; seen.current = { p: v.path, on };
		if (on && !was.on && was.p === v.path && !reduceMotion()) { setPop(true); clearTimeout(t.current); t.current = setTimeout(() => setPop(false), 320); }
	}, [on, v.path]);
	return html`<button class=${cls + ' like' + (on ? ' on' : '') + (pop ? ' pop' : '')} aria-pressed=${on} aria-label=${on ? 'Unlike' : 'Like'} title=${on ? 'Liked · click to unlike' : 'Like'}
		onClick=${e => { e.stopPropagation(); setLike(v.path, !on, '', v.here ? v.path : 'current'); }}><${Icon} n=${on ? 'heart' : 'heart-o'} k=${2} /></button>`;
}
export function showInFiles(p) { go(filesHash(dirName(p))); setTimeout(() => markFresh(p), 300); }

/* ================= Add to playlist ================= */
// the picker's order: the playlists added to last first (as this browser remembers), then A–Z
export function plUsed() { try { return JSON.parse(lsGet('bpPlUsed') || '{}') || {}; } catch (e) { return {}; } }
export function markUsed(id) { const u = plUsed(); u[id] = Date.now(); lsSet('bpPlUsed', JSON.stringify(u)); }
// paths: what's added (files, or a folder's media); what: its name, for the panel and the toast. anchor: the bar's
// button on a computer, which the panel pops over (a sheet otherwise)
export function addToPlaylist(paths, what, anchor) {
	if (anchor && !compactNow()) {
		const r = anchor.getBoundingClientRect();
		const pos = { left: Math.max(8, Math.min(innerWidth - 408, r.left - 160)), bottom: innerHeight - r.top + 12 };
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
	// the three added to last say RECENT
	const recent = new Set(pls.filter(p => used[p.id]).slice(0, 3).map(p => p.id));
	const many = pls.length > 7, f = find.trim().toLowerCase();
	const hit = p => !f || p.name.toLowerCase().includes(f);
	// what matches first; the rest stays, dimmed
	const matches = pls.filter(hit), shown = f ? [...matches, ...pls.filter(p => !hit(p))] : pls;
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
	const findKey = e => { if (e.key !== 'Enter') return; e.preventDefault(); if (matches.length) add(matches[0]); else if (f) create(find); };
	// Esc leaves the name field; a second one closes
	const nameKey = e => { if (e.key === 'Escape') { e.preventDefault(); e.stopPropagation(); setNaming(null); setErr(''); } };
	const hl = n => { if (!f) return n; const i = n.toLowerCase().indexOf(f); return i < 0 ? n : html`${n.slice(0, i)}<span class="hl">${n.slice(i, i + f.length)}</span>${n.slice(i + f.length)}`; };
	const body = !loaded ? html`<div class="none">Loading…</div>` : html`
		${many && html`<label class="find"><${Icon} n="search" k=${2} /><input class="field" ref=${findRef} value=${find} placeholder="Find a playlist" aria-label="Find a playlist" autocomplete="off" enterkeyhint="go" onInput=${e => { setFind(e.target.value); setArmed(null); }} onKeyDown=${findKey} />${!touchOnly && html`<span class="key">Enter</span>`}</label>`}
		<div class="picklist">
			${naming != null ? html`<form class="newpl" onSubmit=${e => { e.preventDefault(); create(nameRef.current ? nameRef.current.value : naming); }}>
				<input class=${'field' + (err ? ' bad' : '')} ref=${nameRef} value=${naming} placeholder="Playlist name" aria-label="New playlist's name" aria-invalid=${err ? 'true' : undefined} autocomplete="off" enterkeyhint="done" onInput=${e => { setNaming(e.target.value); setErr(''); }} onKeyDown=${nameKey} />
				<button class="btn primary" disabled=${busy}>Create</button></form>${err && html`<div class="errtxt" role="alert">${err}</div>`}`
			: html`<button class="sheet-item new" onClick=${() => { setNaming(find.trim()); setErr(''); }}><span class="tile"><${Icon} n="plus" k=${2} /></span><span class="grow">${f && !exact ? 'New playlist “' + find.trim() + '”' : 'New playlist…'}</span></button>`}
			${shown.map(p => { const arm = armed === p.id; return html`<button class=${'sheet-item' + (arm ? ' armed' : '') + (f && !hit(p) ? ' miss' : '')} key=${p.id} disabled=${busy} onClick=${() => add(p)}>
				<span class="tile"><${PlArt} p=${p} /></span><span class="grow">${hl(p.name)}<span class="sm">${arm ? 'Already in it' : html`${recent.has(p.id) && html`<span class="kicker" style="font-size:10px">Recent</span> · `}${plural(p.count, 'item', 'items')}`}</span></span>
				${arm ? html`<span class="r">Add again</span>` : has[p.id] ? html`<span class="r chip ghost"><${Icon} n="check" k=${1} />Added</span>` : null}</button>`; })}
			${f && !matches.length && html`<div class="none">No playlist called “${find.trim()}”</div>`}</div>`;
	const head = html`<div class="uphead"><div class="kicker">Add to playlist</div><h2 class="ell">${what}</h2></div>`;
	if (pos) return html`<div class="popcatch" onMouseDown=${onClose}></div>
		<div class="addpop box surface addto" role="dialog" aria-label=${'Add ' + what + ' to a playlist'} ref=${ref} style=${`left:${pos.left}px;bottom:${pos.bottom}px`}>
			${head}<div class="body">${body}</div></div>`;
	return html`<${Modal} cls="addto" label=${'Add ' + what + ' to a playlist'} raw=${true} onClose=${onClose} actions=${html`<button class="btn" onClick=${onClose}>Cancel</button>`}>${head}<div class="body">${body}</div></${Modal}>`;
}

