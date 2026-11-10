#pragma once
// The Session's host on the handheld (docs/design/session.md §2). Step (a): a SHADOW. Today's code calls the hooks
// below where it acts (opens, steps, toggles, the page's words, the hand-off, Stop, a photo over music); the host turns
// them into intents, runs the reducer beside today's flags, executes no effect, and logs `session: disagree <what>`
// when its output, file or play state differs from today's for a while. It never touches mpv and never blocks.
#include "session.h"
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace bp
{
	class Player;
	class Ui;
	int sourceKindOf(const Player& p);

	class SessionHost
	{
	public:
		static SessionHost& shadow();	// the one on the main thread (only the main thread calls it)

		const Session& session() const { return s; }
		uint64_t boot() const { return s.boot; }
		uint64_t version() const { return s.v; }
		std::string file() const { return s.index >= 0 ? s.queue[s.index] : ""; }
		// the v of the last snapshot published to the pages: what an old page's word is taken to have seen (its basis)
		uint64_t publishedV = 0;

		// an intent made here: origin's next seq (the host numbers each origin's words while the old routes carry
		// none), basis the current v (a legacy page: publishedV)
		Intent make(IntentType t, const std::string& origin = "hh", bool legacyBasis = false);
		Result submit(const Intent& i);

		// ---- hooks from today's code ----
		// can a browser play this file (the web serves it, a format a browser plays): the context Open and Step need
		// (main sets it; none: no)
		std::function<bool(const std::string&)> browserCan;
		// a pick (handheld or web) as Open; sourceKind 0 folder, 1 network, 2 playlist. out: given when the picker says
		// where (the page's Play here / Play in this browser); else the session's pref decides
		void open(const std::vector<std::string>& paths, int index, int sourceKind, const std::string& title,
			const std::string& origin, const Output* out, double t);
		void step(int n, const std::string& origin = "hh");
		void toggle(const std::string& origin = "hh");	// the opposite of the session's playing
		void play(bool on, const std::string& origin = "hh");
		void takeOver();	// the handheld plays again (hold A, the take-over check): SetOutput(hh) + Play
		void stop(const std::string& origin, bool legacyBasis);
		void photoPark(double pos);
		void photoResume();
		void fileGone(const std::string& path);
		void seek(double t, bool relative, const std::string& origin);
		void loop(int value);
		void woke();
		// the page's old words: browser-play (file: what it plays), its stop, browser-pos, browser-refuse; /api/play
		// (after today's code opened it: paths/index are the player's now)
		void pagePlays(const std::string& tab, const std::string& file, const std::vector<std::string>& paths, int index);
		void pagePos(const std::string& tab, const std::string& file, double t, bool playing);
		void pageRefused();
		// /api/play (= /api/handoff): the sound back here, then what today's code did. opened: it opened a file at once (the
		// player's list now); else the same file plays on (seek t, play), or main opens it as the loop comes round (a
		// playlist: the handheld's Open then goes here, the pref being the handheld)
		void handheldAsked(const Player& p, bool opened, double t);

		// each pass of main's loop: the handheld's reports (as the output), Tick, the tabs, and the comparison.
		// tabUp: is that tab's event stream connected (empty function: no dashboard)
		void pass(Player& p, Ui& u, const std::function<bool(const std::string&)>& tabUp);

	private:
		SessionHost();
		Session s;
		std::map<std::string, uint64_t> seqs;
		uint32_t tickAt = 0, posAt = 0, tabsAt = 0, mismatchSince = 0;
		std::map<std::string, bool> tabState;
		std::string pendingSig, loggedSig;
		uint32_t pendingAt = 0, loggedAt = 0;
		uint64_t erroredLoad = 0;
		void compare(Player& p, Ui& u, uint32_t now);
	};
}
