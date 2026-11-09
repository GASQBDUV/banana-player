import { ctl } from './browser-player.js';
import { connAnswered, connFail, signOutHere } from './connection.js';
import { seeVersion, toast } from './toasts.js';
import { store } from './store.js';

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
export async function fetchT(url, init, ms) {
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
export const get = (path, params, opts) => api(path, Object.assign({ _get: 1 }, params), 'GET', opts);
export const post = (path, params) => api(path, params || {}, 'POST');
export const errMsg = e => e.status === 0 ? "Can't reach your handheld" : e.status === 503 ? 'Your handheld is busy. Try again in a moment.' : e.message;
// a failed request's toast; none while the connection banner or screen already says it
export const errToast = e => { if (e && e.status === 0 && store.conn !== 'ok') return; toast(errMsg(e), null, 0, e && e.status === 0); };

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

