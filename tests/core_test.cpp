// The parts without a window: aspect modes, track labels, mpv's track-list, times.
#include "aspect.h"
#include "browser.h"
#include "format.h"
#include "history.h"
#include "playlist.h"
#include "resume.h"
#include "settings.h"
#include "tracks.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#include <map>
#include <string>
#include <vector>

using namespace bp;

static int failures = 0;
#define CHECK_EQ(a, b) \
	do { auto va = (a); auto vb = (b); \
		if (!(va == vb)) { failures++; fprintf(stderr, "%s:%d: %s != %s\n", __FILE__, __LINE__, #a, #b); } } while (0)

static void testAspect()
{
	CHECK_EQ(nextAspect(AspectMode::Fit), AspectMode::Fill);
	CHECK_EQ(nextAspect(AspectMode::Fill), AspectMode::Stretch);
	CHECK_EQ(nextAspect(AspectMode::Stretch), AspectMode::Fit);
	AspectMode m = AspectMode::Fit;
	CHECK_EQ(parseAspect("stretch", m), true);
	CHECK_EQ(m, AspectMode::Stretch);
	CHECK_EQ(parseAspect("zoom", m), false);
	CHECK_EQ(m, AspectMode::Stretch);

	// every mode sets the same properties, so switching never leaves one behind
	auto props = [](AspectMode a) {
		std::map<std::string, std::string> out;
		for (auto& p : aspectProperties(a)) out[p.first] = p.second;
		return out;
	};
	auto fit = props(AspectMode::Fit), fill = props(AspectMode::Fill), stretch = props(AspectMode::Stretch);
	CHECK_EQ(fit.size(), fill.size());
	CHECK_EQ(fit.size(), stretch.size());
	CHECK_EQ(fit["keepaspect"], std::string("yes"));
	CHECK_EQ(fit["panscan"], std::string("0.0"));
	CHECK_EQ(fill["keepaspect"], std::string("yes"));
	CHECK_EQ(fill["panscan"], std::string("1.0"));
	CHECK_EQ(stretch["keepaspect"], std::string("no"));
}

static void testTime()
{
	CHECK_EQ(formatTime(0), std::string("0:00"));
	CHECK_EQ(formatTime(75.9), std::string("1:15"));
	CHECK_EQ(formatTime(3725), std::string("1:02:05"));
	CHECK_EQ(formatTime(-1), std::string("--:--"));
}

static void testLabels()
{
	Track a;
	a.type = TrackType::Audio;
	a.lang = "eng";
	a.codec = "ac3";
	a.channels = 6;
	CHECK_EQ(trackLabel(a, 1), std::string("English · AC3 5.1"));

	a.title = "Director's commentary";
	a.codec = "aac";
	a.channels = 2;
	CHECK_EQ(trackLabel(a, 2), std::string("English · AAC Stereo · Director's commentary"));

	Track s;
	s.type = TrackType::Sub;
	s.codec = "subrip";
	s.external = true;
	CHECK_EQ(trackLabel(s, 3), std::string("Track 3 · SRT · File"));

	s.lang = "jpn";
	s.title = "Japanese";	// says nothing the language doesn't
	s.codec = "hdmv_pgs_subtitle";
	s.external = false;
	s.forced = true;
	CHECK_EQ(trackLabel(s, 1), std::string("Japanese · PGS · Forced"));

	CHECK_EQ(languageName("ZH"), std::string("Chinese"));
	CHECK_EQ(languageName("und"), std::string(""));
	CHECK_EQ(languageName("tlh"), std::string("tlh"));
	CHECK_EQ(codecName("pcm_s16le"), std::string("PCM"));
	CHECK_EQ(codecName("h264"), std::string("H264"));
}

// mpv_node trees as mpv hands them out
struct NodeMap
{
	std::vector<std::string> keyStore;
	std::vector<char*> keys;
	std::vector<mpv_node> values;
	mpv_node_list list{};
	mpv_node node{};

	void str(const char* k, const char* v) { add(k).u.string = const_cast<char*>(v), values.back().format = MPV_FORMAT_STRING; }
	void i64(const char* k, int64_t v) { add(k).u.int64 = v, values.back().format = MPV_FORMAT_INT64; }
	void flag(const char* k, bool v) { add(k).u.flag = v, values.back().format = MPV_FORMAT_FLAG; }
	mpv_node& add(const char* k)
	{
		keyStore.push_back(k);
		values.push_back({});
		return values.back();
	}
	mpv_node* done()
	{
		keys.clear();
		for (auto& k : keyStore) keys.push_back(const_cast<char*>(k.c_str()));
		list.num = (int)values.size();
		list.values = values.data();
		list.keys = keys.data();
		node.format = MPV_FORMAT_NODE_MAP;
		node.u.list = &list;
		return &node;
	}
};

static void testTrackList()
{
	NodeMap v, a1, a2, s1;
	v.i64("id", 1); v.str("type", "video"); v.str("codec", "h264"); v.flag("selected", true);
	a1.i64("id", 1); a1.str("type", "audio"); a1.str("lang", "jpn"); a1.str("codec", "opus");
	a1.i64("demux-channel-count", 2); a1.flag("selected", true); a1.flag("default", true);
	a2.i64("id", 2); a2.str("type", "audio"); a2.str("lang", "eng"); a2.str("codec", "eac3"); a2.i64("demux-channel-count", 6);
	s1.i64("id", 1); s1.str("type", "sub"); s1.str("lang", "eng"); s1.str("codec", "ass"); s1.str("title", "Signs & Songs");
	s1.flag("forced", true);

	std::vector<mpv_node> items = {*v.done(), *a1.done(), *a2.done(), *s1.done()};
	mpv_node_list l{};
	l.num = (int)items.size();
	l.values = items.data();
	mpv_node arr{};
	arr.format = MPV_FORMAT_NODE_ARRAY;
	arr.u.list = &l;

	auto t = parseTrackList(&arr);
	CHECK_EQ(t.size(), (size_t)4);
	CHECK_EQ(t[0].type, TrackType::Video);
	CHECK_EQ(t[1].type, TrackType::Audio);
	CHECK_EQ(t[1].selected, true);
	CHECK_EQ(t[1].isDefault, true);
	CHECK_EQ(t[2].id, (int64_t)2);
	CHECK_EQ(t[2].channels, 6);
	CHECK_EQ(trackLabel(t[2], 2), std::string("English · E-AC3 5.1"));
	CHECK_EQ(t[3].type, TrackType::Sub);
	CHECK_EQ(trackLabel(t[3], 1), std::string("English · ASS · Signs & Songs · Forced"));

	CHECK_EQ(parseTrackList(nullptr).size(), (size_t)0);
}

static void testSpeed()
{
	CHECK_EQ(stepSpeed(1.0, 1), 1.5);
	CHECK_EQ(stepSpeed(2.5, 1), 2.5);		// stops at the top
	CHECK_EQ(stepSpeed(2.5, 1, true), 0.5);	// a tap goes round
	CHECK_EQ(stepSpeed(0.5, -1), 0.5);
	CHECK_EQ(stepSpeed(1.25, -1), 1.0);	// from a speed set elsewhere: the next one down
	CHECK_EQ(speedLabel(1.5), std::string("1.5x"));
	CHECK_EQ(speedLabel(1.0), std::string("1x"));
	CHECK_EQ(formatBytes(512), std::string("512 B"));
	CHECK_EQ(formatBytes(1536), std::string("1.5 KB"));
	CHECK_EQ(formatBytes(734003200), std::string("700 MB"));
	CHECK_EQ(formatBitrate(128000), std::string("128 kb/s"));
	CHECK_EQ(formatBitrate(4500000), std::string("4.5 Mb/s"));
	CHECK_EQ(timeLeft(400, 1000, 1), 600.0);
	CHECK_EQ(timeLeft(400, 1000, 2), 300.0);	// at 2x it's over in half the time
	CHECK_EQ(timeLeft(1200, 1000, 1), 0.0);
	CHECK_EQ(timeLeft(-1, 1000, 1), -1.0);	// not known yet
	CHECK_EQ(timeLeft(10, -1, 1), -1.0);
}

static void testSubStyle()
{
	SubStyle def;
	CHECK_EQ(styleValueLabel(def, FONT), std::string("Inter"));
	CHECK_EQ(styleValueLabel(def, SIZE), std::string("Medium"));
	CHECK_EQ(styleValueLabel(def, BORDER), std::string("Normal"));

	SubStyle s;
	stepStyle(s, COLOR, 1);
	stepStyle(s, FONT, -1);	// wraps to the last
	CHECK_EQ(styleValueLabel(s, COLOR), std::string("Yellow"));
	CHECK_EQ(styleValueLabel(s, FONT), std::string("Fusion Pixel"));

	std::map<std::string, std::string> props;
	for (auto& p : subStyleProperties(s)) props[p.first] = p.second;
	CHECK_EQ(props["sub-color"], std::string("#FFE45C"));
	CHECK_EQ(props["sub-font"], std::string("Fusion Pixel 12px Prop zh-Hans"));
	CHECK_EQ(props["sub-border-size"], std::string("3"));
	CHECK_EQ(props.size(), (size_t)6);

	stepStyle(s, FONT, -2);	// Inter Bold: the same family, bold
	for (auto& p : subStyleProperties(s)) props[p.first] = p.second;
	CHECK_EQ(props["sub-font"], std::string("Inter"));
	CHECK_EQ(props["sub-bold"], std::string("yes"));
	CHECK_EQ(subStyleLook(s).fontFile, std::string("Inter-Bold.ttf"));
	CHECK_EQ(subStyleLook(s).color, 0xFFE45Cu);

	CHECK_EQ(parseSubStyle(serializeSubStyle(s)) == s, true);
	// unknown keys and values are left out; what's missing keeps its default
	SubStyle odd = parseSubStyle("size = huge\ncolor=cyan\nnonsense\n# comment = x\n");
	CHECK_EQ(styleValueLabel(odd, SIZE), std::string("Medium"));
	CHECK_EQ(styleValueLabel(odd, COLOR), std::string("Cyan"));
}

static void testSettings()
{
	Settings d = parseSettings("");
	CHECK_EQ(d.rememberPosition && d.rememberAudio && d.rememberSubs, true);	// all on by default
	Settings s;
	s.rememberAudio = false;
	stepStyle(s.style, SIZE, 1);
	Settings back = parseSettings(serializeSettings(s));
	CHECK_EQ(back.rememberPosition, true);
	CHECK_EQ(back.rememberAudio, false);
	CHECK_EQ(back.rememberSubs, true);
	CHECK_EQ(back.style == s.style, true);
	CHECK_EQ(parseSettings("remember-subtitles = maybe\n").rememberSubs, true);
	CHECK_EQ(d.rememberFolder, false);	// the browser's folder: off by default
	CHECK_EQ(d.keepHistory, true);		// the play history: on by default
	CHECK_EQ(d.buttonHotkeys, true);	// the buttons over a playing video: on by default
	CHECK_EQ(parseSettings("button-hotkeys = no\n").buttonHotkeys, false);
	CHECK_EQ(d.showHiddenFolders, false);
	CHECK_EQ(parseSettings("show-hidden-folders = yes\n").showHiddenFolders, true);
	CHECK_EQ(d.audioDisplay, (int)AUDIO_AUTO);
	Settings vis;
	vis.audioDisplay = AUDIO_SPECTRUM;
	CHECK_EQ(parseSettings(serializeSettings(vis)).audioDisplay, (int)AUDIO_SPECTRUM);
	CHECK_EQ(audioVisualizerGraph(AUDIO_COVER), std::string(""));
	CHECK_EQ(audioVisualizerGraph(AUDIO_WAVES).rfind("[aid1] asplit [ao] [v]; [v] showwaves", 0), (size_t)0);
	CHECK_EQ(std::string(audioDisplayLabel(AUDIO_BARS)), std::string("Bars"));
	CHECK_EQ(audioVisualizerGraph(AUDIO_COVER_BARS).find("[vid1]") != std::string::npos, true);
	CHECK_EQ(parseSettings("audio-display = cover-bars\n").audioDisplay, (int)AUDIO_COVER_BARS);
	CHECK_EQ(parseSettings(serializeSettings(d)).keepHistory, true);
	CHECK_EQ(parseSettings("keep-history = no\n").keepHistory, false);
	CHECK_EQ(parseSettings("remember-folder = yes\n").rememberFolder, true);
}

static void testResume()
{
	ResumeStore r;
	ResumeEntry a;
	a.path = "/data/roms/videos/Show/ep 1.mkv";
	a.position = 754.5;
	a.aid = 2;
	a.sid = 0;
	r.put(a);
	ResumeEntry b;
	b.path = "/data/roms/videos/tab\there.mp4";	// a tab in the name survives: the path is last
	b.sid = 3;
	r.put(b);
	r.lastAudioLang = "jpn";
	r.lastFolder = "/data/roms/videos/Show";
	r.lastSubOff = true;

	ResumeStore back;
	back.parse(r.serialize());
	const ResumeEntry* e = back.find(a.path);
	CHECK_EQ(e != nullptr, true);
	if (e)
	{
		CHECK_EQ(e->position, 754.5);
		CHECK_EQ(e->aid, (int64_t)2);
		CHECK_EQ(e->sid, (int64_t)0);
	}
	e = back.find(b.path);
	CHECK_EQ(e != nullptr && e->position < 0 && e->aid == -1 && e->sid == 3, true);
	CHECK_EQ(back.lastAudioLang, std::string("jpn"));
	CHECK_EQ(back.lastSubOff, true);
	CHECK_EQ(back.lastFolder, std::string("/data/roms/videos/Show"));
	CHECK_EQ(back.all().size(), (size_t)2);

	// put again: replaced, not added
	a.position = 10;
	back.put(a);
	CHECK_EQ(back.find(a.path)->position, 10.0);
	for (int i = 0; i < 400; i++)
	{
		ResumeEntry x;
		x.path = "/v/" + std::to_string(i);
		back.put(x);
	}
	CHECK_EQ(back.find("/v/399") != nullptr, true);
	CHECK_EQ(back.find(a.path) == nullptr, true);	// the oldest went

	CHECK_EQ(worthResuming(2, 1000), false);	// the first seconds
	CHECK_EQ(worthResuming(500, 1000), true);
	CHECK_EQ(worthResuming(990, 1000), false);	// the credits
	CHECK_EQ(worthResuming(120, -1), true);		// unknown length
}

static void testBrowser()
{
	CHECK_EQ(parentFolder("/data/roms/videos/Show"), std::string("/data/roms/videos"));
	CHECK_EQ(parentFolder("/data/roms/videos/Show/"), std::string("/data/roms/videos"));
	CHECK_EQ(parentFolder("/data"), std::string("/"));
	CHECK_EQ(parentFolder("/"), std::string("/"));

	char dir[] = "/tmp/bp-browser-XXXXXX";
	if (!mkdtemp(dir)) { failures++; return; }
	std::string d = dir;
	mkdir((d + "/Season 10").c_str(), 0755);
	mkdir((d + "/Season 2").c_str(), 0755);
	mkdir((d + "/.dotted").c_str(), 0755);
	for (const char* n : {"b.mkv", "A.mp4", "a.srt", ".c.mkv"}) std::ofstream(d + "/" + n) << "x";
	bool ok = false;
	auto e = listFolder(d, ok);
	CHECK_EQ(ok, true);
	CHECK_EQ(e.size(), (size_t)4);	// two folders, two videos; no subtitles, nothing hidden
	if (e.size() == 4)
	{
		CHECK_EQ(e[0].name, std::string("Season 2"));
		CHECK_EQ(e[0].dir, true);
		CHECK_EQ(e[1].name, std::string("Season 10"));
		CHECK_EQ(e[2].name, std::string("A.mp4"));
		CHECK_EQ(e[3].path, d + "/b.mkv");
		CHECK_EQ(e[3].dir, false);
	}
	// a folder with a ".hidden" file: left out unless asked for
	std::ofstream(d + "/Season 2/.hidden") << "";
	std::ofstream(d + "/Season 2/x.mkv") << "x";
	CHECK_EQ(listFolder(d, ok).size(), (size_t)3);
	CHECK_EQ(listFolder(d, ok, true).size(), (size_t)4);
	CHECK_EQ(isHiddenFolder(d + "/Season 2"), true);
	CHECK_EQ(insideHiddenFolder(d + "/Season 2/x.mkv"), true);
	CHECK_EQ(insideHiddenFolder(d + "/A.mp4"), false);
	unlink((d + "/Season 2/.hidden").c_str());
	unlink((d + "/Season 2/x.mkv").c_str());
	listFolder(d + "/missing", ok);
	CHECK_EQ(ok, false);
	for (const char* n : {"b.mkv", "A.mp4", "a.srt", ".c.mkv"}) unlink((d + "/" + n).c_str());
	for (const char* n : {"Season 10", "Season 2", ".dotted"}) rmdir((d + "/" + n).c_str());
	rmdir(dir);
}

static void testHistory()
{
	PlayHistory h;
	h.add("/v/a.mkv");
	h.add("/v/b.mkv");
	h.add("/v/a.mkv");	// again: first, once
	CHECK_EQ(h.items().size(), (size_t)2);
	CHECK_EQ(h.items()[0], std::string("/v/a.mkv"));
	for (int i = 0; i < 40; i++) h.add("/v/" + std::to_string(i));
	CHECK_EQ(h.items().size(), PlayHistory::MAX);	// 30 at most: the oldest go
	CHECK_EQ(h.items()[0], std::string("/v/39"));
	CHECK_EQ(h.items().back(), std::string("/v/10"));

	PlayHistory back;
	back.parse(h.serialize() + "/v/39\n");	// a repeat in the file is read once
	CHECK_EQ(back.items() == h.items(), true);
	back.clear();
	CHECK_EQ(back.items().empty(), true);
}

static void testPlaylist()
{
	CHECK_EQ(isVideoFile("a.MKV"), true);
	CHECK_EQ(isVideoFile("a.srt"), false);
	CHECK_EQ(isVideoFile(".hidden.mp4"), false);
	for (const char* a : {"a.mp3", "b.FLAC", "c.ogg", "d.wav", "e.m4a", "f.opus"})
	{
		CHECK_EQ(isAudioFile(a), true);
		CHECK_EQ(isMediaFile(a), true);
	}
	CHECK_EQ(isAudioFile("a.mkv"), false);
	CHECK_EQ(isMediaFile("cover.jpg"), false);
	CHECK_EQ(naturalLess("ep2.mkv", "ep10.mkv"), true);
	CHECK_EQ(naturalLess("ep10.mkv", "ep2.mkv"), false);
	CHECK_EQ(naturalLess("Ep 02.mkv", "ep 3.mkv"), true);
	CHECK_EQ(naturalLess("a.mkv", "B.mkv"), true);

	char dir[] = "/tmp/bp-playlist-XXXXXX";
	if (!mkdtemp(dir)) { failures++; return; }
	std::string d = dir;
	for (const char* n : {"ep10.mkv", "ep2.mkv", "ep1.mp4", "ep2.srt", "notes.txt"}) std::ofstream(d + "/" + n) << "x";
	int index = -1;
	auto list = siblingVideos(d + "/ep2.mkv", index);
	CHECK_EQ(list.size(), (size_t)3);
	CHECK_EQ(index, 1);
	if (list.size() == 3)
	{
		CHECK_EQ(list[0], d + "/ep1.mp4");
		CHECK_EQ(list[2], d + "/ep10.mkv");
	}
	auto alone = siblingVideos(d + "/missing.mkv", index);
	CHECK_EQ(alone.size(), (size_t)1);
	CHECK_EQ(index, 0);
	for (const char* n : {"ep10.mkv", "ep2.mkv", "ep1.mp4", "ep2.srt", "notes.txt"}) unlink((d + "/" + n).c_str());
	rmdir(dir);
}

int main()
{
	testSpeed();
	testSubStyle();
	testSettings();
	testResume();
	testPlaylist();
	testBrowser();
	testHistory();
	testAspect();
	testTime();
	testLabels();
	testTrackList();
	if (failures) fprintf(stderr, "%d failed\n", failures);
	else printf("core: all passed\n");
	return failures != 0;
}
