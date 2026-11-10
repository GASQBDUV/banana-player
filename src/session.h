#pragma once
// One playback Session (docs/design/session.md): what plays, where, and how far, as one value changed only by intents
// through apply(), a pure reducer. No window, no mpv, no network, no clock: `now` (the host's ms clock) is passed in,
// so a sequence of intents replays exactly (the fuzz test's seeds).
#include <cstdint>
#include <string>
#include <vector>

namespace bp
{
	enum class OutKind { Handheld, Browser };
	// where the sound and picture are: the handheld, or one browser tab (its TAB_ID)
	struct Output
	{
		OutKind kind = OutKind::Handheld;
		std::string tab;
		bool operator==(const Output& o) const { return kind == o.kind && tab == o.tab; }
		bool operator!=(const Output& o) const { return !(*this == o); }
	};
	inline Output handheldOut() { return {}; }
	inline Output browserOut(const std::string& tab) { return {OutKind::Browser, tab}; }

	// an audio or subtitle choice, as each renderer can map it onto its own track list (matchTrack). ord: the place
	// within its kind (stream order, side files after), -1 off; of: how many of that kind the chooser listed (the place
	// counts only when the other renderer lists as many); set=false: the renderer's default
	struct TrackPick
	{
		int ord = -1, of = 0;
		std::string lang, label;
		bool external = false, set = false;
		bool operator==(const TrackPick& o) const
		{
			return ord == o.ord && of == o.of && lang == o.lang && label == o.label && external == o.external && set == o.set;
		}
		bool operator!=(const TrackPick& o) const { return !(*this == o); }
	};
	// one track as a renderer lists it (playable: that renderer can play it; Chrome can't DTS)
	struct TrackInfo { std::string lang, label; bool external = false, playable = true; };
	constexpr int MATCH_OFF = -1, MATCH_DEFAULT = -2;
	// the pick on this renderer's list: an index, MATCH_OFF or MATCH_DEFAULT (by place when both list as many, else by
	// label, else by language, else the default; a match it can't play is the default too)
	int matchTrack(const TrackPick& pick, const std::vector<TrackInfo>& list);

	// where the queue came from (a mirror of player.h's PlaySource, which bpcore can't include: it pulls in mpv).
	// kind: 0 folder, 1 network folder, 2 playlist
	struct SessionSource
	{
		int kind = 0;
		std::string title, id;
		bool shuffled = false;
		bool operator==(const SessionSource& o) const { return kind == o.kind && title == o.title && id == o.id && shuffled == o.shuffled; }
	};

	// the last word of one origin (the idempotency store): a retry with a seq at or under it gets this back
	struct OriginSeen
	{
		std::string origin;
		uint64_t seq = 0, v = 0;
		std::string reject;
		bool operator==(const OriginSeen& o) const { return origin == o.origin && seq == o.seq && v == o.v && reject == o.reject; }
	};
	constexpr size_t SEEN_MAX = 16;

	constexpr double RESTART_S = 5;	// Prev this far in or more: back to the start (Player::RESTART_S)
	constexpr int64_t LOAD_TIMEOUT_MS = 8000;	// an output that never says Loaded: it can't play it
	constexpr int64_t TAB_GRACE_MS = 10000;	// the output's tab gone this long: the handheld, paused
	constexpr int SESSION_LOOP_OFF = 0, SESSION_LOOP_ALL = 1, SESSION_LOOP_ONE = 2;	// settings.h's LOOP_*

	struct Session
	{
		uint64_t boot = 0, v = 0;	// boot: random per process; (boot, v) orders every Session
		std::vector<std::string> queue;	// real paths, in play order, no photos
		int index = -1;	// -1: nothing (stopped)
		SessionSource source;
		uint64_t load = 0;	// bumps whenever the item (re)loads or changes output: the token for reports
		bool playing = false;	// the wish; the output's renderer makes it true
		double pos = 0;	// seconds, as of posAt (the host's ms clock); moved only by the output
		int64_t posAt = 0;
		double speed = 1;
		int loop = SESSION_LOOP_OFF;
		TrackPick audio, sub;
		Output out, pref;	// out: plays now; pref: where picks go (a browser that couldn't play one stays pref)
		uint64_t outV = 0, itemV = 0, playV = 0, seekN = 0;	// the v of the last output / item / play-state change
		bool parked = false;	// a photo holds the handheld's mpv
		std::string error;	// the item failed on the output; Step still works
		double dur = 0;	// the item's length once Loaded (0: not known)
		bool audioOnly = false;	// the item is a song
		bool loaded = false;	// the output said Loaded for this load
		// host timers (not published, not a change of v): when this load was asked for; the output's tab gone since
		int64_t loadAt = 0, goneAt = 0;
		std::string goneTab;
		std::vector<OriginSeen> seen;	// the last SEEN_MAX origins, least recently used first
	};

	// where it has got to by now: pos, run on while it plays on a loaded output, within the item's length
	double posNow(const Session& s, int64_t now);
	// the handheld's mpv sounds only for this (I2)
	inline bool hhShouldPlay(const Session& s) { return s.out.kind == OutKind::Handheld && s.playing && !s.parked && s.index >= 0; }
	// the origin whose reports count: "hh", or the output's tab
	std::string outOrigin(const Session& s);
	// a client's rule for snapshots: another boot (the handheld restarted) resets, else only a newer v is taken
	inline bool takesSnapshot(uint64_t seenBoot, uint64_t seenV, uint64_t boot, uint64_t v) { return boot != seenBoot || v > seenV; }

	enum class IntentType
	{
		Open, Play, Pause, Seek, SeekBy, Step, SetAudio, SetSub, SetSpeed, SetLoop, SetOutput, Stop, PhotoPark,
		PhotoResume, FileGone, Loaded, Pos, Ended, CantPlay, Tick, TabGone, TabBack, Slept, Woke
	};
	const char* intentName(IntentType t);

	// one intent: {origin, seq, basis} and its fields (those of its type; the rest are ignored)
	struct Intent
	{
		IntentType type = IntentType::Tick;
		std::string origin = "hh";	// "hh" or a tab id
		uint64_t seq = 0;	// grows per origin; 0: not deduplicated (the host's own: ticks, tabs, sleep)
		uint64_t basis = 0;	// the v the sender last saw
		// Open
		std::vector<std::string> queue;
		int index = 0;
		SessionSource source;
		bool hasOut = false;	// Open, SetOutput: out says where
		Output out;
		bool hasAudio = false, hasSub = false;
		TrackPick audio, sub;	// Open (with hasAudio/hasSub), SetAudio, SetSub
		// Open, Step: the host's word that a browser can play the item (served, a format it plays); else the handheld
		bool browserCan = true;
		// Seek, Open (start), Pos (t), SeekBy (d); PhotoPark and SetOutput (at: the old output's position) only with hasT
		double t = 0;
		bool hasT = false;
		int n = 0;	// Step
		double speed = 1;	// SetSpeed
		int loop = 0;	// SetLoop
		std::string path;	// FileGone; TabGone / TabBack: the tab
		uint64_t load = 0;	// reports
		double dur = 0;	// Loaded
		bool audioOnly = false;	// Loaded
		bool playing = false;	// Pos
		std::string reason;	// CantPlay: "format", "net", "timeout", "error"
	};

	struct Effect
	{
		enum Kind { HhLoad, HhPlay, HhPause, HhSeek, HhTracks, HhSpeed, HhUnload, HhPhoto, Note, PageToast } kind = Note;
		std::string path, text;	// HhLoad: the file; Note / PageToast: what to say
		double t = 0;	// HhLoad (start), HhSeek
		uint64_t load = 0;	// HhLoad
		bool hold = false;	// HhLoad: held paused for another output (screens show it, it never sounds)
		bool aoReload = false;	// HhPlay: the first play of a copy that was only held (silent ALSA otherwise)
		bool operator==(const Effect& o) const
		{
			return kind == o.kind && path == o.path && text == o.text && t == o.t && load == o.load && hold == o.hold && aoReload == o.aoReload;
		}
	};
	const char* effectName(Effect::Kind k);

	struct Result
	{
		Session s;
		std::vector<Effect> fx;
		std::string reject;	// "" applied; else why not (the session is the old one, its seen store aside)
		bool dup = false;	// a seq seen before: the earlier answer, nothing done again
	};

	// pure, total, deterministic
	Result apply(const Session& s, const Intent& i, int64_t now);
	// the effects of one loop pass as the executor runs them: the last HhLoad only, HhSeek only after it, notes once
	std::vector<Effect> coalesce(const std::vector<Effect>& fx);
	// the first invariant the session breaks ("I7: index out of the queue"), "" none. checkTransition adds those of one
	// apply (I2 on the effects, I3, I4, I8, I10)
	std::string checkInvariants(const Session& s);
	std::string checkTransition(const Session& old, const Intent& i, const Result& r, int64_t now);
	// "v3→v4 fx=[HhLoad,HhPlay]" or "rejected (stale)": a log line's tail
	std::string describe(const Session& old, const Intent& i, const Result& r);
}
