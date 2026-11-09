import { html } from '../vendor/htm-preact-standalone.module.js';

/* ================= small components ================= */
export const dimsCache = {};
export function Icon({ n, k, cls, label }) {
	let d = dimsCache[n];
	if (!d) { const s = document.getElementById('g-' + n); const vb = s && s.viewBox.baseVal; d = dimsCache[n] = vb ? [vb.width, vb.height] : [9, 9]; }
	k = k || 2;
	return html`<svg class="ic ${cls || ''}" width=${d[0] * k} height=${d[1] * k} viewBox="0 0 ${d[0]} ${d[1]}" aria-hidden=${label ? undefined : 'true'} role=${label ? 'img' : undefined} aria-label=${label}><use href="#g-${n}"/></svg>`;
}
// a redraw every ms while the page shows (none while hidden: battery)
