#include "session_host.h"
#include "player.h"
#include "ui.h"
#include <SDL.h>
#include <cstdio>
#include <random>

namespace bp
{
	SessionHost& SessionHost::shadow()
	{
		static SessionHost host;
		return host;
	}

	SessionHost::SessionHost()
	{
		std::random_device rd;
		s.boot = ((uint64_t)rd() << 32 | rd()) & 0x1FFFFFFFFFFFFFull;	// (an exact number in JavaScript)
	}

	static std::string base(const std::string& p) { return p.empty() ? "-" : p.substr(p.rfind('/') + 1); }

	Intent SessionHost::make(IntentType t, const std::string& origin, bool legacyBasis)
	{
		Intent i;
		i.type = t;
		i.origin = origin;
		bool host = t == IntentType::Tick || t == IntentType::TabGone || t == IntentType::TabBack || t == IntentType::Slept ||
			t == IntentType::Woke;
		i.seq = host ? 0 : ++seqs[origin];
		i.basis = legacyBasis ? publishedV : s.v;
		return i;
	}

	Result SessionHost::submit(const Intent& in)
	{
		uint32_t now = SDL_GetTicks();
		Intent i = in;
		Result r = apply(s, i, now);
		// a new item routed to the browser that the browser can't have (not served, a format it doesn't play): again,
		// with the host's word (the reducer can't know, §5)
		if (r.reject.empty() && r.s.index >= 0 && r.s.itemV == s.v + 1 && r.s.out.kind == OutKind::Browser &&
			(!browserCan || !browserCan(r.s.queue[r.s.index])))
		{
			i.browserCan = false;
			r = apply(s, i, now);
		}
		bool quiet = (i.type == IntentType::Tick && r.s.v == s.v && r.fx.empty()) ||
			(i.type == IntentType::Pos && r.s.v == s.v) || r.reject == "stale-report";
		if (!quiet)
		{
			std::string extra;
			if (r.reject.empty() && r.s.v != s.v)
			{
				extra = " out=" + std::string(r.s.out.kind == OutKind::Handheld ? "hh" : r.s.out.tab) + " file=" +
					base(r.s.index >= 0 ? r.s.queue[r.s.index] : "") + (r.s.playing ? " playing" : " paused");
			}
			fprintf(stderr, "session: %s%s%s\n", describe(s, i, r).c_str(), i.origin == "hh" ? "" : (" from " + i.origin).c_str(), extra.c_str());
		}
		s = r.s;
		return r;
	}

	void SessionHost::open(const std::vector<std::string>& paths, int index, int sourceKind, const std::string& title,
		const std::string& origin, const Output* out, double t)
	{
		Intent i = make(IntentType::Open, origin);
		i.queue = paths;
		i.index = index;
		i.source.kind = sourceKind;
		i.source.title = title;
		i.t = t;
		if (out)
		{
			i.hasOut = true;
			i.out = *out;
		}
		submit(i);
	}

	void SessionHost::step(int n, const std::string& origin)
	{
		Intent i = make(IntentType::Step, origin);
		i.n = n;
		submit(i);
	}

	void SessionHost::toggle(const std::string& origin) { submit(make(s.playing ? IntentType::Pause : IntentType::Play, origin)); }
	void SessionHost::play(bool on, const std::string& origin) { submit(make(on ? IntentType::Play : IntentType::Pause, origin)); }

	void SessionHost::takeOver()
	{
		Intent i = make(IntentType::SetOutput);
		i.hasOut = true;
		i.out = handheldOut();
		submit(i);
		play(true);
	}

	void SessionHost::stop(const std::string& origin, bool legacyBasis) { submit(make(IntentType::Stop, origin, legacyBasis)); }

	void SessionHost::photoPark(double pos)
	{
		Intent i = make(IntentType::PhotoPark);
		i.hasT = pos >= 0;
		i.t = pos;
		submit(i);
	}

	void SessionHost::photoResume() { submit(make(IntentType::PhotoResume)); }

	void SessionHost::fileGone(const std::string& path)
	{
		Intent i = make(IntentType::FileGone, "web");
		i.path = path;
		submit(i);
	}

	void SessionHost::seek(double t, bool relative, const std::string& origin)
	{
		Intent i = make(relative ? IntentType::SeekBy : IntentType::Seek, origin);
		i.t = t;
		submit(i);
	}

	void SessionHost::loop(int value)
	{
		Intent i = make(IntentType::SetLoop, "web");
		i.loop = value;
		submit(i);
	}

	void SessionHost::woke() { submit(make(IntentType::Woke)); }

	void SessionHost::pagePlays(const std::string& tab, const std::string& path, const std::vector<std::string>& paths, int index)
	{
		if (path.empty() || tab.empty()) return;
		std::string origin = tab;
		if (s.index >= 0 && file() == path)
		{
			if (s.out != browserOut(tab))
			{
				// "Play in this browser" of what plays here, or the page's answer to a pick sent to it
				Intent i = make(IntentType::SetOutput, origin, true);
				i.hasOut = true;
				i.out = browserOut(tab);
				submit(i);
				if (s.out == browserOut(tab) && !s.playing) play(true, origin);
			}
		}
		else
		{
			// the page chose another file itself (its Next, a tap in its list): the handheld's list as it opened it
			Intent i = make(IntentType::Open, origin);
			i.queue = paths.empty() ? std::vector<std::string>{path} : paths;
			i.index = paths.empty() ? 0 : index;
			i.hasOut = true;
			i.out = browserOut(tab);
			submit(i);
		}
		// it says it plays it: its Loaded
		if (s.out == browserOut(tab) && s.index >= 0 && file() == path && !s.loaded)
		{
			Intent l = make(IntentType::Loaded, origin);
			l.load = s.load;
			submit(l);
		}
	}

	void SessionHost::pagePos(const std::string& tab, const std::string& path, double t, bool playing)
	{
		// the old route carries a file, not a load: one about another file is for an older load
		Intent i = make(IntentType::Pos, tab.empty() ? "web" : tab, true);
		i.load = s.index >= 0 && file() == path ? s.load : s.load - (s.load > 0);
		i.t = t;
		i.playing = playing;
		submit(i);
	}

	void SessionHost::pageRefused()
	{
		Intent i = make(IntentType::CantPlay, s.out.kind == OutKind::Browser ? s.out.tab : "web");
		i.load = s.load;
		i.reason = "format";
		submit(i);
	}

	int sourceKindOf(const Player& p)
	{
		SourceKind k = p.source().kind;
		return k == SourceKind::Playlist ? 2 : k == SourceKind::NetFolder ? 1 : 0;
	}

	void SessionHost::handheldAsked(const Player& p, bool opened, double t)
	{
		Output hh = handheldOut();
		Intent i = make(IntentType::SetOutput, "web");
		i.hasOut = true;
		i.out = hh;
		submit(i);
		if (opened)
		{
			open(p.paths(), p.index(), sourceKindOf(p), p.source().title, "web", &hh, t);
			return;
		}
		if (s.index >= 0 && p.playing() && file() == p.currentPath())
		{
			if (t > 0) seek(t, false, "web");
			play(true, "web");
		}
	}

	void SessionHost::pass(Player& p, Ui& u, const std::function<bool(const std::string&)>& tabUp)
	{
		uint32_t now = SDL_GetTicks();
		// the handheld as the output: its reports (it is the clock)
		if (s.index >= 0 && s.out.kind == OutKind::Handheld && !s.parked && p.playing() && !p.playingPhoto() && p.currentPath() == file())
		{
			if (!p.error.empty())
			{
				if (erroredLoad != s.load)
				{
					erroredLoad = s.load;
					Intent i = make(IntentType::CantPlay);
					i.load = s.load;
					i.reason = "error";
					submit(i);
				}
			}
			else if (!s.loaded)
			{
				if (p.loaded && !p.loading())
				{
					Intent i = make(IntentType::Loaded);
					i.load = s.load;
					i.dur = std::max(0.0, p.duration);
					i.audioOnly = p.playingAudio();
					submit(i);
					posAt = now;
				}
			}
			else
			{
				// its play state, once it settles (mpv answers a pause a moment later; the comparison sees it first)
				bool agree = p.paused == !s.playing;
				if (agree) mismatchSince = 0;
				else if (!mismatchSince) mismatchSince = now;
				if (now - posAt >= 1000 && (agree || now - mismatchSince >= 3000))
				{
					posAt = now;
					Intent i = make(IntentType::Pos);
					i.load = s.load;
					i.t = std::max(0.0, p.position);
					i.playing = !p.paused;
					submit(i);
				}
			}
		}
		if (now - tickAt >= 250)
		{
			tickAt = now;
			submit(make(IntentType::Tick));
		}
		if (now - tabsAt >= 500)
		{
			tabsAt = now;
			for (const Output* o : {&s.out, &s.pref})
			{
				if (o->kind != OutKind::Browser) continue;
				std::string tab = o->tab;
				bool up = tabUp && tabUp(tab);
				auto it = tabState.find(tab);
				bool was = it == tabState.end() ? true : it->second;
				tabState[tab] = up;
				if (up != was)
				{
					Intent i = make(up ? IntentType::TabBack : IntentType::TabGone);
					i.path = tab;
					submit(i);
				}
			}
		}
		compare(p, u, now);
	}

	void SessionHost::compare(Player& p, Ui& u, uint32_t now)
	{
		bool photo = p.playingPhoto();
		bool page = u.browserPlaying || (u.pageOut && !p.playing());
		std::string todayFile = p.playing() && !photo ? p.currentPath() : "";
		std::string today = page ? "browser" : !todayFile.empty() ? "handheld" : photo && u.parkedShown() ? "photo" : "none";
		std::string mine = s.index < 0 ? "none" : s.out.kind == OutKind::Browser ? "browser" : s.parked ? "photo" : "handheld";
		std::string sig;
		if (mine != today) sig = "output (session " + mine + ", today " + today + ")";
		else if ((mine == "handheld" || mine == "browser") && !todayFile.empty() && todayFile != file())
			sig = "file (session " + base(file()) + ", today " + base(todayFile) + ")";
		else if (mine == "browser" && !u.browserTab.empty() && u.browserTab != s.out.tab)
			sig = "tab (session " + s.out.tab + ", today " + u.browserTab + ")";
		else if (mine == "handheld" && p.loaded && !p.loading() && p.error.empty() && s.playing == p.paused)
			sig = std::string("playing (session ") + (s.playing ? "1" : "0") + ", today " + (p.paused ? "0" : "1") + ")";
		else if (mine == "browser" && s.playing != u.pagePlaying())
			sig = std::string("playing (session ") + (s.playing ? "1" : "0") + ", today " + (u.pagePlaying() ? "1" : "0") + ")";
		if (sig.empty())
		{
			if (!loggedSig.empty()) fprintf(stderr, "session: agrees again (v%llu)\n", (unsigned long long)s.v);
			loggedSig.clear();
			pendingSig.clear();
			return;
		}
		// async on both sides (mpv, the page): only what lasts 1.5 s, one line per 2 s at most
		if (sig != pendingSig)
		{
			pendingSig = sig;
			pendingAt = now;
			return;
		}
		if (sig == loggedSig || now - pendingAt < 1500 || now - loggedAt < 2000) return;
		loggedSig = sig;
		loggedAt = now;
		fprintf(stderr, "session: disagree %s v%llu file=%s\n", sig.c_str(), (unsigned long long)s.v, base(file()).c_str());
	}
}
