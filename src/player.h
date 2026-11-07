#pragma once
#include "aspect.h"
#include "history.h"
#include "resume.h"
#include "settings.h"
#include "tracks.h"
#include <mpv/client.h>
#include <mpv/render_gl.h>
#include <string>
#include <vector>

namespace bp
{
	struct PlayerOptions
	{
		std::string stateDir;	// settings.conf and resume.tsv; empty: nothing kept
		std::string lang;		// preferred audio and subtitle languages, mpv's alang/slang ("eng,en")
		std::string fontsDir;	// the subtitle fonts (assets/fonts)
		bool verbose = false;
	};

	// libmpv: demuxing, decoding, A/V sync, tracks and subtitles; the playlist and what is remembered of each file.
	// The window and the buttons are the caller's.
	class Player
	{
	public:
		~Player() { shutdown(); }

		// wakeup is called on mpv's threads when events are waiting: the caller wakes its loop and calls pollEvents
		bool init(const PlayerOptions& o, void (*wakeup)(void*), void* ctx);
		// OpenGL (or GLES) context current on this thread; update is called on mpv's threads when a frame is due
		bool initRender(void* (*getProc)(void*, const char*), void* procCtx, void (*update)(void*), void* ctx);
		void shutdown();

		// plays list[index], then the others in order with next() and prev()
		void open(const std::vector<std::string>& list, int index);
		bool next();	// false on the last one
		bool prev();	// false on the first one
		// at a file's end: the next one, or (Loop all, music) the first again; false when that's all
		bool advance();
		bool playingAudio() const { return isAudio && !playlist.empty(); }
		int index() const { return current; }
		int count() const { return (int)playlist.size(); }

		// applies what mpv has sent since the last call
		void pollEvents();
		// called now and then: sound that didn't open is opened again
		void checkAudio();
		bool frameDue();
		void render(int w, int h);
		void reportSwap();

		void togglePause();
		// from any thread: the lid closed, the handheld slept
		void pauseNow();
		void seekRelative(double seconds);
		void scrub(double fraction);		// fast (to a keyframe): while a finger drags the bar
		void seekFraction(double fraction);	// to the frame
		void setSpeed(double s);
		// a choice of the player's: also the language a file opened for the first time starts with
		void selectTrack(TrackType type, int64_t id);
		void setAspect(AspectMode m);
		// applies at once and keeps them in settings.conf
		void setSettings(const Settings& s);
		void osd(const std::string& text, int ms = 1500);
		// the volume/brightness card over the top panel ("Volume", 85), till hideLevel
		void showLevel(const std::string& label, int percent);
		void hideLevel();
		// the top panel with nothing playing: the icon, the name, where to pick something
		void showIdle(bool on);
		// keeps the file's position and tracks in resume.tsv, as far as the settings say
		void saveResume();
		// back to no file (the browser): the place kept first
		void stop();
		bool playing() const { return !playlist.empty(); }
		std::string currentPath() const { return playlist.empty() ? "" : playlist[current]; }
		const ResumeStore& resumeStore() const { return resume; }
		const PlayHistory& history() const { return played; }
		void clearHistory();
		// the browser's folder, kept (in resume.tsv) only while Remember last folder is on
		void setLastFolder(const std::string& dir);

		double position = -1, duration = -1, speed = 1;
		bool paused = false, loaded = false;
		bool finished = false;	// the file played to its end (the caller moves on, then clears it)
		bool quit = false;		// mpv itself is going away
		AspectMode aspect = AspectMode::Fit;
		Settings settings;
		std::string title, error;
		std::vector<Track> tracks;

		// the Media info page: sections (header rows) of label/value rows, read from mpv as it plays
		struct InfoRow
		{
			std::string label, value;
			bool header = false;
		};
		std::vector<InfoRow> mediaInfo() const;
		// one of mpv's properties as text ("mpv-version", "ffmpeg-version"...); "" where mpv has none
		std::string property(const char* name) const { return get(name); }

		std::vector<Track> tracksOf(TrackType type) const;
		const Track* selected(TrackType type) const;

	private:
		void load();
		void refreshTracks();
		void prop(const char* name, const std::string& value);
		// an audio file: the visualizer (Audio display) and the title, artist and album over the top panel
		void applyAudioDisplay();
		int shownMusic() const;	// MusicShow as it comes out for this file (its art or none)
		void updateOverlay();
		bool isAudio = false;
		std::string get(const char* name) const;	// "" where mpv has none
		void writeFile(const std::string& path, const std::string& text);

		mpv_handle* mpv = nullptr;
		mpv_render_context* rc = nullptr;
		PlayerOptions opts;
		std::vector<std::string> playlist;
		int current = 0;
		ResumeStore resume;
		PlayHistory played;
		int64_t wantAid = 0, wantSid = 0;
		// the tracks playing, as last seen while the file was open: at its end mpv has dropped its track list
		int64_t seenAid = -1, seenSid = -1;
		uint64_t audioCheckAt = 0;
		int audioChecks = 0;
		static uint64_t ticks();	// asked for at load: checked once the file's tracks are known
	};
}
