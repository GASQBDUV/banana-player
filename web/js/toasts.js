import { html, render } from '../vendor/htm-preact-standalone.module.js';
import { notify, set, store } from './store.js';
import { reduceMotion } from './playing.js';
import { Modal } from './components.js';
import { B, el } from './browser-player.js';
import { uploading } from './uploads.js';

/* ================= toasts, modals ================= */
let toastId = 0;
export function toast(text, action, ms, offline) {
	const id = ++toastId;
	ms = ms || (action ? 6000 : 4000);
	set({ toasts: [...store.toasts.filter(t => t.text !== text).slice(-2), { id, text, action, offline, ms }] });
	// it sinks away, then goes
	setTimeout(() => { const t = store.toasts.find(x => x.id === id); if (!t) return; t.out = true; notify(); setTimeout(() => set({ toasts: store.toasts.filter(x => x.id !== id) }), 200); }, ms);
	return id;
}
let modalId = 0;
// onDismiss: called when the dialog goes without an answer (Esc, the scrim, the connection lost)
export function openModal(renderFn, onDismiss) {
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
export function closeOverlays() { store.modals.slice().forEach(m => m.dismiss()); set({ modals: [], lightbox: null, help: false }); }
// which way the last track change went: the cover and title come in from that side
export let trackDir = 0, trackDirAt = 0;
export const markTrack = d => { trackDir = d; trackDirAt = Date.now(); };
export function ask(title, sub, opts) {
	opts = opts || {};
	return new Promise(res => {
		openModal(close => html`<${Modal} title=${title} sub=${sub} onClose=${() => { close(); res(null); }}
			actions=${html`${(opts.buttons || [{ label: 'Cancel', value: null }, { label: opts.ok || 'OK', value: true, cls: 'primary' }]).map(b =>
				html`<button class="btn ${b.cls || ''}" data-focus=${b.focus ? '1' : undefined} onClick=${() => { close(); res(b.value); }}>${b.label}</button>`)}`}>${opts.body}</${Modal}>`, () => res(null));
	});
}

/* the web app's build: a tab left open across an update reloads to the new one, but never in the middle of something */
let webVersion = null;
export function seeVersion(v) {
	if (!v) return;	// an older player says nothing
	if (webVersion == null) { webVersion = v; return; }
	if (v === webVersion || store.updated) return;
	const busy = (B.active && (!el.paused || B.parked != null)) || uploading() || store.modals.length || store.lightbox;
	if (!busy) { location.reload(); return; }
	set({ updated: true });
}

