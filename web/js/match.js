// The Session's track matching (session.cpp's matchTrack, the same rule): a pick {set, ord, of, lang, label, external}
// on a renderer's list [{lang, label, external, playable}] is an index, MATCH_OFF or MATCH_DEFAULT. By place when both
// list as many, else by label, else by language, else the default; a match it can't play is the default too.
// tests/session_vectors.json holds cases both run (core-test, tools/web-smoke.sh)
export const MATCH_OFF = -1, MATCH_DEFAULT = -2;
export function matchTrack(pick, list) {
	if (!pick || !pick.set) return MATCH_DEFAULT;
	if (pick.ord < 0) return MATCH_OFF;
	const can = k => list[k].playable !== false;
	if (pick.of > 0 && list.length === pick.of && pick.ord < list.length) return can(pick.ord) ? pick.ord : MATCH_DEFAULT;
	if (pick.label) {
		let any = -1;
		for (let k = 0; k < list.length; k++) if (list[k].label === pick.label) { if (can(k)) return k; if (any < 0) any = k; }
		if (any >= 0) return MATCH_DEFAULT;
	}
	if (pick.lang) {
		const want = pick.lang.toLowerCase();
		for (let k = 0; k < list.length; k++) if (can(k) && (list[k].lang || '').toLowerCase() === want && !!list[k].external === !!pick.external) return k;
		for (let k = 0; k < list.length; k++) if (can(k) && (list[k].lang || '').toLowerCase() === want) return k;
	}
	return MATCH_DEFAULT;
}
