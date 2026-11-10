import { html, useState, useEffect } from '../vendor/htm-preact-standalone.module.js';
import { NO_HERE, NO_HERE_WHY, set, store, useStore } from './store.js';
import { baseName, dirName, fmtTime, plural } from './util.js';
import { errMsg, errToast, get, post } from './api.js';
import { playAt, playHere, view } from './browser-player.js';
import { Art, Empty, MoreDots, Skeleton, coverUrl, openSheet, useLater } from './components.js';
import { go } from './router.js';
import { addToPlaylist, isLiked, setLike, showInFiles } from './plshared.js';
import { ask, toast } from './toasts.js';
import { Icon } from './icons.js';
import { GLYPH } from './files.js';

/* ================= Recent: what the handheld's Home strip shows (its history, newest first, with the places kept) ================= */
export const RECENT_ID = '~recent';
// the list, shared by the Playlists page's card and the Recent page; again on every "recent" event
store.recent = null; store.recentTick = 0;
let seq = 0;
export async function loadRecent() {
	const n = ++seq;
	try { const r = await get('/api/recent', null, { quiet: true }); if (n === seq) set({ recent: r }); }
	catch (e) { if (n === seq && !store.recent) set({ recent: { error: errMsg(e) } }); }
}
export function useRecent() {
	const s = useStore();
	useEffect(() => { loadRecent(); }, [s.recentTick, s.epoch]);
	return s.recent;
}
// Recent's cover: the clock on the dark of Liked's heart
export const RecentArt = ({ k }) => html`<span class="recent-art gc"><${Icon} n="clock" k=${k || 2} /></span>`;

// "0:27 / 0:52", "Played · 3:10"
const noteOf = it => it.played ? 'Played' + (it.duration > 0 ? ' · ' + fmtTime(it.duration) : '') : fmtTime(it.position) + (it.duration > 0 ? ' / ' + fmtTime(it.duration) : '');
// its folder's name; a place's own name at the top of one
const folderOf = p => { const d = dirName(p); return d.includes('/') ? baseName(d) : (store.roots && store.roots[+d] && store.roots[+d].name) || 'This card'; };
export const playRecent = (it, target) => playAt(it.p, it.played ? 0 : it.position, target, true);

async function removeIt(it) {
	try { await post('/api/recent/remove', { p: it.p }); toast('Removed from Recent'); loadRecent(); } catch (e) { errToast(e); }
}
export async function clearRecent() {
	if (!await ask('Clear Recent?', 'The places your videos were left at are kept.', { buttons: [{ label: 'Cancel', value: null }, { label: 'Clear', value: true, cls: 'danger' }] })) return;
	try { await post('/api/recent/clear', {}); toast('Recent cleared'); loadRecent(); } catch (e) { errToast(e); }
}

export function RecentView() {
	const r = useRecent(), v = view(), nowPath = v.has && !v.finished ? v.path : '';
	const slow = useLater(!r, 300);
	const items = r && r.items || [];
	const menu = anchor => openSheet('Recent', 'clock', [{ label: 'Clear Recent', icon: 'trash', danger: true, disabled: !items.length, fn: clearRecent }], { anchor });
	const rowMenu = (it, anchor) => {
		const liked = isLiked(it.p);
		openSheet(it.title, GLYPH[it.kind] || 'note', it.gone ? [{ label: 'Remove from Recent', icon: 'minus', sub: 'The file is gone from the card', fn: () => removeIt(it) }] : [
			{ label: 'Play on handheld', icon: 'card', fn: () => playRecent(it, 'handheld') },
			{ label: 'Play in this browser', icon: 'laptop', disabled: NO_HERE, sub: NO_HERE ? NO_HERE_WHY : undefined, fn: () => playHere(() => playRecent(it, 'browser'), it.p) }, '-',
			{ label: liked ? 'Unlike' : 'Like', icon: liked ? 'heart-o' : 'heart', fn: () => setLike(it.p, !liked, 'menu') },
			{ label: 'Add to playlist…', icon: 'pl-add', fn: () => addToPlaylist([it.p], it.title) },
			{ label: 'Show in Files', icon: 'folder', fn: () => showInFiles(it.p) }, '-',
			{ label: 'Remove from Recent', icon: 'minus', fn: () => removeIt(it) }], { anchor, sub: noteOf(it), seed: it.p, kind: it.kind, cover: coverUrl(it.p) });
	};
	const tap = it => it.gone ? toast('That file is gone from the card') : playHere(t => playRecent(it, t), it.p);
	const head = html`<div class="phead" style="padding-bottom:0"><div class="crumbs"><nav><button aria-label="Back to playlists" onClick=${() => go('#/playlists')}><${Icon} n="left" k=${1} />Playlists</button></nav></div></div>
		<div class="plhero"><span class="cv"><${RecentArt} k=${6} /></span>
			<div class="grow" style="min-width:180px"><div class="kicker y" style="margin-bottom:8px">From the handheld · newest first</div><h1>Recent</h1>
				<div class="meta2">${!r ? '' : r.on === false ? 'Play history is off' : items.length ? plural(items.length, 'item', 'items') : 'Nothing yet'}</div></div>
			<div class="acts"><button class="btn icon" aria-label="More about Recent" aria-haspopup="dialog" onClick=${e => menu(e.currentTarget)}><${MoreDots} /></button></div></div>`;
	if (r && r.error) return html`<div class="page">${head}<${Empty} icon="alert" title="Couldn't load Recent" sub=${r.error} button="Try again" onClick=${loadRecent} /></div>`;
	if (!r) return html`<div class="page">${head}${slow ? html`<${Skeleton} />` : null}</div>`;
	if (!items.length) return html`<div class="page">${head}<${Empty} icon="clock" title="Nothing played yet" sub=${r.on === false ? 'Play history is off in the handheld’s Settings.' : 'Songs and videos you play, here or on the handheld, show up here.'} button="Go to Files" onClick=${() => go('#/files')} /></div>`;
	return html`<div class="page">${head}
		<div class="box surface listbox"><div class="flist" role="list">${items.map(it => { const cur = it.p === nowPath; return html`<div class=${'pitem nogrip recent' + (it.gone ? ' gone' : '') + (cur ? ' cur' : '')} role="listitem" key=${it.p}>
			<button class="main-btn tt" onClick=${() => tap(it)} onContextMenu=${e => { if (matchMedia('(hover: hover)').matches) { e.preventDefault(); rowMenu(it, { x: e.clientX, y: e.clientY }); } }}>
				<span class="tile"><${Art} src=${it.gone ? '' : coverUrl(it.p)} seed=${it.p} kind=${it.kind} />${!it.played && it.duration > 0 && html`<span class="rprog" aria-hidden="true"><i style=${'width:' + Math.round(it.frac * 100) + '%'}></i></span>`}</span>
				<span class="grow"><div class="fn ell">${it.title}</div>
					<div class="fm">${cur && html`<span class="chip y tiny"><${Icon} n="play" k=${1} />Playing</span>`}${it.gone && html`<span class="chip red tiny">Gone</span>`}<span class="ell">${folderOf(it.p)}</span></div></span>
				<span class=${'dur' + (it.played ? '' : ' y')}>${noteOf(it)}</span></button>
			<button class="ib more" aria-label=${'More about ' + it.title} aria-haspopup="dialog" onClick=${e => rowMenu(it, e.currentTarget)}><${MoreDots} /></button></div>`; })}</div></div></div>`;
}
