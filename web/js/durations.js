// Songs' and videos' lengths for the lists that have none (Up next from a folder or this browser's queue, Files):
// asked of /api/durations only for the rows that come into view, 32 at a time, one request at a time, and kept for
// the session ("none" too: a broken file is never asked again). Network files are never asked (each would be a read
// over the network).
import { useEffect, useState } from '../vendor/htm-preact-standalone.module.js';
import { post } from './api.js';
import { kindOfName } from './util.js';

const known = new Map();		// path → seconds, -1 none
const wanted = new Set();		// seen, not asked yet
const asked = new Set();		// in the request now
const listeners = new Set();
let busy = false, timer = 0;

// a file whose length is worth asking: on the card (a root's index first), a song or a video whose kind keeps its
// length in its header (not MPEG-TS, MPEG-PS/VOB or raw AAC: durationInHeader in src/durations.cpp)
export const durable = p => /^\d+\//.test(p) && /^(audio|video)$/.test(kindOfName(p)) && !/\.(ts|m2ts|mts|m2t|tp|trp|mpg|mpeg|vob|aac)$/i.test(p);
// seconds, or 0 while unknown (or none)
export const durationOf = p => { const d = known.get(p); return d > 0 ? d : 0; };

async function flush() {
	timer = 0;
	if (busy) return;
	const batch = [...wanted].filter(p => !known.has(p) && !asked.has(p)).slice(0, 32);
	batch.forEach(p => { wanted.delete(p); asked.add(p); });
	if (!batch.length) return;
	busy = true;
	let got = false;
	try {
		const r = await post('/api/durations', { p: batch }) || {};
		for (const p of batch) if (typeof r[p] === 'number') { known.set(p, r[p] > 0 ? r[p] : -1); got = true; }
		// (one left out: the handheld ran out of time; asked again when its row next comes into view)
	} catch (e) {}
	busy = false;
	batch.forEach(p => asked.delete(p));
	if (got) listeners.forEach(f => f());
	if (wanted.size) timer = setTimeout(flush, 50);
}
function want(p) {
	if (known.has(p) || asked.has(p)) return;
	wanted.add(p);
	if (!timer) timer = setTimeout(flush, 80);	// (the rows of one scroll in one request)
}

// box: a ref to the list; its elements with data-dur="<path>" get their lengths as they come into view (200 px ahead).
// deps: when the rows may have changed. Re-renders the caller as answers come
export function useDurations(box, deps) {
	const [, setTick] = useState(0);
	useEffect(() => {
		const f = () => setTick(t => t + 1);
		listeners.add(f);
		return () => listeners.delete(f);
	}, []);
	useEffect(() => {
		const el = box.current;
		if (!el || typeof IntersectionObserver === 'undefined') return;
		const io = new IntersectionObserver(es => {
			for (const e of es) if (e.isIntersecting) { want(e.target.dataset.dur); io.unobserve(e.target); }
		}, { rootMargin: '200px 0px' });
		el.querySelectorAll('[data-dur]').forEach(r => { if (!known.has(r.dataset.dur)) io.observe(r); });
		return () => io.disconnect();
	}, deps);
}
