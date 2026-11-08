// The parts without a window: aspect modes, track labels, mpv's track-list, times.
#include "aspect.h"
#include "battery.h"
#include "browser.h"
#include "format.h"
#include "ftplist.h"
#include "history.h"
#include "photo.h"
#include "update.h"
#include "backlight.h"
#include "playlist.h"
#include "keyboard.h"
#include "netpath.h"
#include "secret.h"
#include "servers.h"
#include "sidecar.h"
#include "resume.h"
#include "settings.h"
#include "tracks.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
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
	CHECK_EQ(d.musicShow, (int)SHOW_ART);
	CHECK_EQ(d.visualizer, (int)VIS_BARS);
	CHECK_EQ(d.loop, (int)LOOP_OFF);
	Settings m;
	m.musicShow = SHOW_BOTH;
	m.visualizer = VIS_SPECTRUM;
	m.loop = LOOP_ONE;
	Settings mb = parseSettings(serializeSettings(m));
	CHECK_EQ(mb.musicShow == SHOW_BOTH && mb.visualizer == VIS_SPECTRUM && mb.loop == LOOP_ONE, true);
	CHECK_EQ(visualizerGraph(VIS_WAVES, false).rfind("[aid1] asplit [ao] [v]; [v] showwaves", 0), (size_t)0);
	CHECK_EQ(visualizerGraph(VIS_SPECTRUM, true).find("[vid1]") != std::string::npos, true);
	CHECK_EQ(visualizerGraph(VIS_SPECTRUM, true).find("showspectrum=s=640x200") != std::string::npos, true);
	// Circle: drawn from its tables; without them, as Bars
	CHECK_EQ(visualizerGraph(VIS_CIRCLE, false).find("showfreqs") != std::string::npos, true);
	CHECK_EQ(visualizerGraph(VIS_CIRCLE, false).find("remap"), std::string::npos);
	char cdir[] = "/tmp/bp-circle-XXXXXX";
	if (mkdtemp(cdir))
	{
		CHECK_EQ(writeCircleMaps(cdir), true);
		CHECK_EQ(writeCircleMaps(cdir), true);	// kept: not made again
		std::string g = visualizerGraph(VIS_CIRCLE, true, cdir);
		CHECK_EQ(g.find("remap") != std::string::npos && g.find("[vid1]") != std::string::npos, true);
		// no source that never ends: past the song's end it would flood mpv (its clock stopped, gigabytes taken)
		CHECK_EQ(g.find("loop="), std::string::npos);
		CHECK_EQ(visualizerGraph(VIS_CIRCLE, false, cdir).find("loop="), std::string::npos);
		CHECK_EQ(g.find(std::string("movie='") + cdir + "/circle2-x.pgm'") != std::string::npos, true);
		// the middle is black (the palette's last column), straight up from it a bar's lowest step
		std::ifstream in(std::string(cdir) + "/circle2-x.pgm", std::ios::binary);
		std::string pgm((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		size_t head = pgm.size() - (size_t)CIRCLE_W * CIRCLE_H * 2;
		auto at = [&](int x, int y) { size_t i = head + ((size_t)y * CIRCLE_W + x) * 2; return (unsigned char)pgm[i] << 8 | (unsigned char)pgm[i + 1]; };
		CHECK_EQ(at(CIRCLE_W / 2, 274), 256 + 15);			// the middle: black
		CHECK_EQ(at(CIRCLE_W / 2 - 1, 274 - 90) < 256, true);	// straight up: a bar
		CHECK_EQ(at(5, 5), 256 + 15);							// the title's corner: black
		for (const char* f : {"/circle2-x.pgm", "/circle2-y.pgm"}) unlink((std::string(cdir) + f).c_str());
		rmdir(cdir);
	}
	CHECK_EQ(parseSettings("visualizer = circle\n").visualizer, (int)VIS_CIRCLE);
	CHECK_EQ(std::string(loopLabel(LOOP_ALL)), std::string("Loop all"));
	// the setting before 0.1.1, carried over
	Settings old = parseSettings("audio-display = cover-bars\n");
	CHECK_EQ(old.musicShow == SHOW_BOTH && old.visualizer == VIS_BARS, true);
	old = parseSettings("audio-display = waves\n");
	CHECK_EQ(old.musicShow == SHOW_VIS && old.visualizer == VIS_WAVES, true);
	CHECK_EQ(parseSettings("audio-display = auto\n").musicShow, (int)SHOW_ART);
	CHECK_EQ(parseSettings(serializeSettings(d)).keepHistory, true);
	CHECK_EQ(parseSettings("keep-history = no\n").keepHistory, false);
	CHECK_EQ(d.keepNetHistory, true);	// network files in Recent: on by default
	CHECK_EQ(parseSettings("keep-network-history = no\n").keepNetHistory, false);
	CHECK_EQ(parseSettings(serializeSettings(d)).keepNetHistory, true);
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

	{
		// the network's taken out, the card's kept in their order
		PlayHistory m;
		m.add("/card/a.mkv");
		m.add("bpnet://ab12cd34/Media/b.mkv");
		m.add("/card/c.mkv");
		m.removeIf([](const std::string& p) { return isNetPath(p); });
		CHECK_EQ(m.items().size(), (size_t)2);
		CHECK_EQ(m.items()[0], std::string("/card/c.mkv"));
		CHECK_EQ(m.items()[1], std::string("/card/a.mkv"));
	}
	PlayHistory back;
	back.parse(h.serialize() + "/v/39\n");	// a repeat in the file is read once
	CHECK_EQ(back.items() == h.items(), true);
	back.clear();
	CHECK_EQ(back.items().empty(), true);
}

static void testBattery()
{
	char dir[] = "/tmp/bp-power-XXXXXX";
	if (!mkdtemp(dir)) { failures++; return; }
	std::string d = dir;
	CHECK_EQ(readBattery(d + "/none").percent, -1);	// no supplies: no battery
	mkdir((d + "/battery").c_str(), 0755);
	mkdir((d + "/usb").c_str(), 0755);
	std::ofstream(d + "/battery/type") << "Battery\n";
	std::ofstream(d + "/battery/capacity") << "57\n";
	std::ofstream(d + "/battery/status") << "Discharging\n";
	std::ofstream(d + "/usb/type") << "USB\n";
	std::ofstream(d + "/usb/online") << "0\n";
	BatteryState b = readBattery(d);
	CHECK_EQ(b.percent, 57);
	CHECK_EQ(b.charging, false);
	// the rk817's gauge says Discharging on the cable: the charger online says charging
	std::ofstream(d + "/usb/online") << "1\n";
	CHECK_EQ(readBattery(d).charging, true);
	std::ofstream(d + "/usb/online") << "0\n";
	std::ofstream(d + "/battery/status") << "Full\n";
	CHECK_EQ(readBattery(d).charging, true);
	CHECK_EQ(parseSettings("").batteryPercent, false);	// the percent: off by default
	CHECK_EQ(parseSettings("").keepPhotoZoom, true);
	CHECK_EQ(parseSettings("").panRight, false);	// the pan area: left by default
	Settings ph;
	ph.keepPhotoZoom = false;
	ph.panRight = true;
	Settings phb = parseSettings(serializeSettings(ph));
	CHECK_EQ(phb.keepPhotoZoom, false);
	CHECK_EQ(phb.panRight, true);
	CHECK_EQ(parseSettings("").checkUpdates, true);
	CHECK_EQ(parseSettings("check-updates = no\n").checkUpdates, false);
	Settings later;
	later.updateRemindAfter = 1794100000;
	CHECK_EQ(parseSettings(serializeSettings(later)).updateRemindAfter, 1794100000LL);
	CHECK_EQ(parseSettings("").dragBox, true);	// a drag moves the box over the small photo
	CHECK_EQ(parseSettings("pan-drag = photo\n").dragBox, false);
	ph.dragBox = false;
	CHECK_EQ(parseSettings(serializeSettings(ph)).dragBox, false);
	for (const char* f : {"/battery/type", "/battery/capacity", "/battery/status", "/usb/type", "/usb/online"}) unlink((d + f).c_str());
	rmdir((d + "/battery").c_str());
	rmdir((d + "/usb").c_str());
	rmdir(dir);
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
	CHECK_EQ(isAviFile("My Film.AVI"), true);
	CHECK_EQ(isAviFile("bpnet://3a3cff8c/Media/a.divx"), true);
	CHECK_EQ(isAviFile("a.mkv"), false);
	CHECK_EQ(isMediaFile("a.txt"), false);
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

	// photos step through photos, videos and songs through videos and songs
	for (const char* n : {"a.jpg", "b.mkv", "c.PNG", "d.mp3"}) std::ofstream(d + "/" + n) << "x";
	auto photos = siblingVideos(d + "/c.PNG", index);
	CHECK_EQ(photos.size(), (size_t)2);
	CHECK_EQ(index, 1);
	auto media = siblingVideos(d + "/d.mp3", index);
	CHECK_EQ(media.size(), (size_t)2);
	CHECK_EQ(index, 1);
	if (media.size() == 2) CHECK_EQ(media[0], d + "/b.mkv");
	CHECK_EQ(folderHasCover(d + "/d.mp3"), false);	// a.jpg is no cover's name
	std::ofstream(d + "/Folder.JPG") << "x";
	CHECK_EQ(folderHasCover(d + "/d.mp3"), true);
	unlink((d + "/Folder.JPG").c_str());
	for (const char* n : {"a.jpg", "b.mkv", "c.PNG", "d.mp3"}) unlink((d + "/" + n).c_str());
	rmdir(dir);
}

static void testPhoto()
{
	CHECK_EQ(isPhotoFile("IMG_0001.JPG"), true);
	CHECK_EQ(isPhotoFile("a.webp"), true);
	CHECK_EQ(isPhotoFile("a.mkv"), false);
	CHECK_EQ(isMediaFile("cover.jpg"), true);

	CHECK_EQ(stepZoom(1, 1), 1.5);
	CHECK_EQ(stepZoom(1.5, 1), 2.0);
	CHECK_EQ(stepZoom(8, 1), 8.0);
	CHECK_EQ(stepZoom(1, -1), 1.0);
	CHECK_EQ(stepZoom(2.5, -1), 2.0);	// from between steps: the one below
	CHECK_EQ(stepZoom(2.5, 1), 3.0);

	// a 4:3 photo on the 4:3 panel fits exactly; 2x: twice the panel, half a panel each way to move
	PhotoScale s = photoScale(4.0 / 3, 4.0 / 3, false, 1);
	CHECK_EQ(s.x, 1.0);
	CHECK_EQ(s.y, 1.0);
	CHECK_EQ(maxPan(1), 0.0);
	CHECK_EQ(maxPan(2), 0.25);
	// a 2:3 portrait fits by its height: half the panel's width
	s = photoScale(2.0 / 3, 4.0 / 3, false, 1);
	CHECK_EQ(s.x, 0.5);
	CHECK_EQ(s.y, 1.0);
	// filled: as wide as the panel, twice as tall
	s = photoScale(2.0 / 3, 4.0 / 3, true, 1);
	CHECK_EQ(s.x, 1.0);
	CHECK_EQ(s.y, 2.0);

	// an EXIF block: little-endian, IFD0 with Make and Orientation 6, an Exif IFD with DateTimeOriginal
	auto le16 = [](std::string& b, unsigned v) { b += (char)(v & 0xFF); b += (char)(v >> 8); };
	auto le32 = [&](std::string& b, unsigned v) { le16(b, v & 0xFFFF); le16(b, v >> 16); };
	std::string tiff = "II";
	le16(tiff, 42);
	le32(tiff, 8);
	// IFD0 at 8: 3 entries (12 bytes each), next-IFD 0 -> ends at 8 + 2 + 36 + 4 = 50
	le16(tiff, 3);
	le16(tiff, 0x010F); le16(tiff, 2); le32(tiff, 6); le32(tiff, 50);	// Make "Canon\0" at 50
	le16(tiff, 0x0112); le16(tiff, 3); le32(tiff, 1); le32(tiff, 6);
	le16(tiff, 0x8769); le16(tiff, 4); le32(tiff, 1); le32(tiff, 56);	// Exif IFD at 56
	le32(tiff, 0);
	tiff += std::string("Canon\0", 6);
	le16(tiff, 1);	// at 56: one entry, its text at 56 + 2 + 12 + 4 = 74
	le16(tiff, 0x9003); le16(tiff, 2); le32(tiff, 20); le32(tiff, 74);
	le32(tiff, 0);
	tiff += std::string("2024:05:01 13:45:10\0", 20);
	std::string app1 = std::string("Exif\0\0", 6) + tiff;
	std::string jpeg = "\xFF\xD8\xFF\xE1";
	jpeg += (char)((app1.size() + 2) >> 8);
	jpeg += (char)((app1.size() + 2) & 0xFF);
	jpeg += app1 + "\xFF\xDA";
	ExifInfo e = parseExif(jpeg);
	CHECK_EQ(e.orientation, 6);
	CHECK_EQ(e.make, std::string("Canon"));
	CHECK_EQ(e.taken, std::string("2024:05:01 13:45:10"));
	CHECK_EQ(exifRotation(e.orientation), 90);
	CHECK_EQ(exifDate(e.taken), std::string("2024-05-01 13:45"));
	CHECK_EQ(parseExif("not a jpeg").orientation, 1);
	// padded with 0xFF before the marker: still found
	std::string padded = "\xFF\xD8\xFF" + jpeg.substr(2);
	CHECK_EQ(parseExif(padded).orientation, 6);
	// an IFD offset out of the block: nothing read from the header's bytes
	std::string badOff = jpeg;
	badOff[2 + 2 + 2 + 6 + 4] = (char)0xF0;	// IFD0's offset (in the TIFF header, after "II" 42)
	CHECK_EQ(parseExif(badOff).make, std::string());
	CHECK_EQ(exifDate("garbage that is long"), std::string());
	CHECK_EQ(lavfiSafePath("/data/home/.config/banana-player"), true);
	CHECK_EQ(lavfiSafePath("/a/co:lon"), false);
	CHECK_EQ(lavfiSafePath("/a/eq=x"), false);
	CHECK_EQ(visualizerGraph(VIS_CIRCLE, false, "/a/co:lon").find("remap"), std::string::npos);	// as Bars
	CHECK_EQ(exifRotation(8), 270);
}

static void testUpdate()
{
	CHECK_EQ(compareVersions("0.2.10", "0.2.9") > 0, true);
	CHECK_EQ(compareVersions("v0.2.2", "0.2.2"), 0);
	CHECK_EQ(compareVersions("0.2", "0.2.1") < 0, true);
	CHECK_EQ(compareVersions("1.0.0", "0.9.9") > 0, true);
	CHECK_EQ(sha256Hex(""), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
	CHECK_EQ(sha256Hex("abc"), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
	// two blocks (56 bytes: the padding spills over)
	CHECK_EQ(sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
		std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
	CHECK_EQ(parseSha256File("BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD  x.zip\n"),
		std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
	CHECK_EQ(parseSha256File("not a digest"), std::string());

	// GitHub's answer, in its order: the zip's name, size, then its URL (an uploader's object between, no size)
	std::string json = "{\"tag_name\": \"v0.3.0\", \"name\": \"Banana Player 0.3.0\", \"assets\": ["
		"{\"name\": \"BananaPlayer-0.3.0-aarch64.zip\", \"uploader\": {\"login\": \"x\"}, \"size\": 14344040, "
		"\"browser_download_url\": \"https:\\/\\/github.com\\/a\\/BananaPlayer-0.3.0-aarch64.zip\"},"
		"{\"name\": \"BananaPlayer-0.3.0-aarch64.zip.sha256\", \"size\": 97, "
		"\"browser_download_url\": \"https://github.com/a/BananaPlayer-0.3.0-aarch64.zip.sha256\"}]}";
	Release r;
	CHECK_EQ(parseRelease(json, r), true);
	CHECK_EQ(r.version, std::string("0.3.0"));
	CHECK_EQ(r.zipUrl, std::string("https://github.com/a/BananaPlayer-0.3.0-aarch64.zip"));
	CHECK_EQ(r.shaUrl, std::string("https://github.com/a/BananaPlayer-0.3.0-aarch64.zip.sha256"));
	CHECK_EQ(r.zipSize, (int64_t)14344040);
	CHECK_EQ(parseRelease("{\"message\": \"API rate limit exceeded\"}", r), false);

	// tomorrow: after now, within a day, at a midnight
	time_t now = time(nullptr), m = nextMidnight(now);
	struct tm t;
	localtime_r(&m, &t);
	CHECK_EQ(m > now && m - now <= 25 * 3600, true);
	CHECK_EQ(t.tm_hour == 0 && t.tm_min == 0 && t.tm_sec == 0, true);
}

static void testBacklight()
{
	CHECK_EQ(stepBrightness(740, 1), 800);	// between steps (bananad's keys): to the next ten
	CHECK_EQ(stepBrightness(740, -1), 700);
	CHECK_EQ(stepBrightness(700, 1), 800);
	CHECK_EQ(stepBrightness(700, -1), 600);
	CHECK_EQ(stepBrightness(100, -1), 100);	// 10% at least: never off
	CHECK_EQ(stepBrightness(1000, 1), 1000);

	char dir[] = "/tmp/bp-backlight-XXXXXX";
	if (!mkdtemp(dir)) { failures++; return; }
	std::string d = dir;
	Backlight bl;
	bl.sysDir = d + "/sys";
	bl.store = d + "/backlight";
	for (const char* n : {"backlight0", "backlight1"})
	{
		mkdir((d + "/sys").c_str(), 0755);
		mkdir((d + "/sys/" + n).c_str(), 0755);
		std::ofstream(d + "/sys/" + n + "/max_brightness") << "1666\n";
		std::ofstream(d + "/sys/" + n + "/brightness") << "833\n";
	}
	CHECK_EQ(bl.present(Backlight::TOP), true);
	CHECK_EQ(bl.permille(Backlight::BOTTOM), 500);
	// the top (backlight1) to 70%: sysfs as bananad rounds it, the store "<top> <bottom>"
	CHECK_EQ(bl.set(Backlight::TOP, 700), true);
	std::ifstream b1(d + "/sys/backlight1/brightness");
	int v = 0;
	b1 >> v;
	CHECK_EQ(v, (1666 * 700 + 500) / 1000);
	CHECK_EQ(bl.permille(Backlight::TOP), 700);
	std::ifstream st(d + "/backlight");
	int t = 0, b = 0;
	st >> t >> b;
	CHECK_EQ(t, 700);
	CHECK_EQ(b, 500);	// the bottom as it was
	// the bottom next: the top kept from the store
	bl.set(Backlight::BOTTOM, 300);
	std::ifstream st2(d + "/backlight");
	st2 >> t >> b;
	CHECK_EQ(t == 700 && b == 300, true);
	Backlight none;
	none.sysDir = d + "/none";
	CHECK_EQ(bl.present(Backlight::TOP) && !none.present(Backlight::TOP), true);
	system(("rm -rf " + d).c_str());
}

static void testFtpList()
{
	auto m = parseMlsd("type=cdir;modify=20261107; .\r\ntype=pdir; ..\r\ntype=dir;modify=20261107100000; Yellow Album\r\n"
		"type=file;size=1234;modify=20261107100000; Sintel (trailer).mkv\r\ntype=OS.unix=slink:/x;size=3; link\r\nbad line\r\n");
	CHECK_EQ(m.size(), (size_t)3);
	CHECK_EQ(m[0].name, std::string("Yellow Album"));
	CHECK_EQ(m[0].dir, true);
	CHECK_EQ(m[0].size, (int64_t)-1);
	CHECK_EQ(m[1].name, std::string("Sintel (trailer).mkv"));
	CHECK_EQ(m[1].dir, false);
	CHECK_EQ(m[1].size, (int64_t)1234);
	CHECK_EQ(m[2].name, std::string("link"));
	CHECK_EQ(parseMlsd("").size(), (size_t)0);
	CHECK_EQ(parseMlsd("Type=DIR;Size=5; Up\n")[0].dir, true);

	auto u = parseFtpList("total 12\r\ndrwxr-xr-x    2 1000     1000         4096 Nov  7 10:00 .\r\ndrwxr-xr-x    4 1000     1000         4096 Nov  7 10:00 ..\r\n"
		"drwxr-xr-x    2 1000     1000         4096 Nov  7 10:00 My Movies\r\n"
		"-rw-r--r--    1 1000     1000      6049358 Nov  7  2025 Sintel  (trailer).mkv\r\n"
		"lrwxrwxrwx    1 root     root            9 Jan  1 00:00 latest -> Movies/\r\n"
		"lrwxrwxrwx    1 root     root            9 Jan  1 00:00 a.mp3 -> b.mp3\r\n"
		"-rw-r--r--   1 owner 12 Dec 31 23:59 nogroup.txt\r\n");
	CHECK_EQ(u.size(), (size_t)5);
	CHECK_EQ(u[0].name, std::string("My Movies"));
	CHECK_EQ(u[0].dir, true);
	CHECK_EQ(u[1].name, std::string("Sintel  (trailer).mkv"));
	CHECK_EQ(u[1].size, (int64_t)6049358);
	CHECK_EQ(u[2].name, std::string("latest"));
	CHECK_EQ(u[2].dir, true);
	CHECK_EQ(u[3].name, std::string("a.mp3"));
	CHECK_EQ(u[3].dir, false);
	CHECK_EQ(u[4].name, std::string("nogroup.txt"));
	CHECK_EQ(u[4].size, (int64_t)12);

	auto d = parseFtpList("11-07-26  10:00AM       <DIR>          Photos 2026\r\n11-07-2026  02:15PM            6049358 trailer one.mkv\r\n");
	CHECK_EQ(d.size(), (size_t)2);
	CHECK_EQ(d[0].name, std::string("Photos 2026"));
	CHECK_EQ(d[0].dir, true);
	CHECK_EQ(d[1].name, std::string("trailer one.mkv"));
	CHECK_EQ(d[1].size, (int64_t)6049358);
	CHECK_EQ(parseFtpList("garbage\r\n").size(), (size_t)0);
}

static void testNet()
{
	{
		NetServer a;
		a.host = "TONY-PC";
		a.port = 445;
		CHECK_EQ(automaticServerName(a), std::string("TONY-PC"));
		CHECK_EQ(serverName(a), std::string("TONY-PC"));
		a.share = "Media";
		CHECK_EQ(serverName(a), std::string("Media on TONY-PC"));
		a.port = 1445;
		CHECK_EQ(serverName(a), std::string("Media on TONY-PC:1445"));
		a.name = "  ";
		CHECK_EQ(serverName(a), std::string("Media on TONY-PC:1445"));
		a.name = "Living room";
		CHECK_EQ(serverName(a), std::string("Living room"));
		NetServer f;
		f.type = NetType::Ftp;
		f.port = 21;
		CHECK_EQ(serverName(f), std::string("FTP server"));
		f.host = "192.168.1.9";
		CHECK_EQ(serverName(f), std::string("192.168.1.9"));
	}

	CHECK_EQ(isNetPath("bpnet://ab12/x"), true);
	CHECK_EQ(isNetPath("/home/x"), false);
	NetPath n;
	CHECK_EQ(parseNetPath("bpnet://ab12cd34", n), true);
	CHECK_EQ(n.server, std::string("ab12cd34"));
	CHECK_EQ(n.path, std::string("/"));
	CHECK_EQ(parseNetPath("bpnet:///x", n), false);
	CHECK_EQ(parseNetPath("file:///x", n), false);
	CHECK_EQ(makeNetPath("s", "//A/./B/../C//"), std::string("bpnet://s/A/C"));
	CHECK_EQ(makeNetPath("s", "/../.."), std::string("bpnet://s/"));
	CHECK_EQ(makeNetPath("s", ""), std::string("bpnet://s/"));
	CHECK_EQ(netParent("bpnet://s/A/B"), std::string("bpnet://s/A"));
	CHECK_EQ(netParent("bpnet://s/A"), std::string("bpnet://s/"));
	CHECK_EQ(netParent("bpnet://s/"), std::string("bpnet://s/"));
	CHECK_EQ(netBaseName("bpnet://s/A/b.mkv"), std::string("b.mkv"));
	CHECK_EQ(netBaseName("bpnet://s/"), std::string(""));
	CHECK_EQ(netJoin("bpnet://s/", "A"), std::string("bpnet://s/A"));
	CHECK_EQ(netJoin("bpnet://s/A", "b c"), std::string("bpnet://s/A/b c"));
	const char* names[] = {"Sintel (trailer) #1 ?.mkv", "100%.mp4", "\xed\x95\x9c\xea\xb5\xad\xec\x96\xb4.mkv", "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e a.mp4"};
	for (auto nm : names)
	{
		std::string u = netJoin("bpnet://s/Share", nm);
		CHECK_EQ(u.find('?'), std::string::npos);
		CHECK_EQ(u.find('#'), std::string::npos);
		CHECK_EQ(parseNetPath(u, n), true);
		CHECK_EQ(n.path, std::string("/Share/") + nm);
		CHECK_EQ(netBaseName(u), std::string(nm));
		CHECK_EQ(netDecode(netEncode(nm)), std::string(nm));
	}
	CHECK_EQ(netEncode("a%b?c#d\n"), std::string("a%25b%3Fc%23d%0A"));
	CHECK_EQ(netDecode("100%"), std::string("100%"));
	std::string id = newServerId();
	CHECK_EQ(id.size(), (size_t)8);
	CHECK_EQ(id.find_first_not_of("0123456789abcdef"), std::string::npos);

	// servers.conf
	CHECK_EQ(defaultPort(NetType::Smb), 445);
	CHECK_EQ(defaultPort(NetType::Ftp), 21);
	CHECK_EQ(std::string(netTypeLabel(NetType::Ftp)), std::string("FTP"));
	NetServer a;
	a.id = "aaaa1111"; a.name = " NAS = [home]\nline2\\ \xed\x95\x9c "; a.host = "nas.local"; a.user = "tab\tuser";
	a.secret = "v1:abc+/="; a.share = "Media"; a.folder = "/Films"; a.lastUsed = 1700000000123LL;
	NetServer b;
	b.id = "bbbb2222"; b.name = "Cam"; b.host = "10.0.0.2"; b.port = 2121; b.type = NetType::Ftp; b.guest = true; b.ftps = true;
	std::string text = serializeServers({a, b}) + "[other x]\nname = no\n[server cccc3333]\nfuture = 1\nname = C\n";
	auto got = parseServers(text);
	CHECK_EQ(got.size(), (size_t)3);
	if (got.size() == 3)
	{
		CHECK_EQ(got[0].name, a.name);
		CHECK_EQ(got[0].user, a.user);
		CHECK_EQ(got[0].secret, a.secret);
		CHECK_EQ(got[0].folder, a.folder);
		CHECK_EQ(got[0].lastUsed, a.lastUsed);
		CHECK_EQ(got[0].port, 0);
		CHECK_EQ(got[1].port, 2121);
		CHECK_EQ(got[1].type == NetType::Ftp, true);
		CHECK_EQ(got[1].guest, true);
		CHECK_EQ(got[1].ftps, true);
		CHECK_EQ(got[2].name, std::string("C"));
	}
	CHECK_EQ(parseServers("").size(), (size_t)0);

	// secret
	std::string raw;
	for (int i = 0; i < 256; i++) raw += (char)i;
	for (size_t len : {0, 1, 2, 3, 255, 256})
	{
		std::string e = base64Encode(raw.substr(0, len)), d;
		CHECK_EQ(base64Decode(e, d), true);
		CHECK_EQ(d, raw.substr(0, len));
	}
	CHECK_EQ(base64Encode("Man"), std::string("TWFu"));
	CHECK_EQ(base64Encode("Ma"), std::string("TWE="));
	std::string tmp;
	CHECK_EQ(base64Decode("TW=u", tmp), false);
	CHECK_EQ(base64Decode("TWF", tmp), false);
	std::string k1 = deriveKey("file-key", "serial1"), k2 = deriveKey("file-key", "serial2");
	CHECK_EQ(k1.size(), (size_t)32);
	CHECK_EQ(k1 == k2, false);
	CHECK_EQ(deriveKey("file-key", "serial1") == k1, true);
	CHECK_EQ(deriveKey("file-key", "").size(), (size_t)32);
	for (std::string pw : {std::string("hunter2 \xed\x95\x9c"), std::string(""), raw})
	{
		std::string s = sealSecret(pw, k1), out = "x";
		CHECK_EQ(s.compare(0, 3, "v1:"), 0);
		CHECK_EQ(openSecret(s, k1, out), true);
		CHECK_EQ(out, pw);
		CHECK_EQ(openSecret(s, k2, out), false);
		CHECK_EQ(sealSecret(pw, k1) == s, false);	// a fresh nonce each time
	}
	std::string s = sealSecret("pw", k1), out;
	for (size_t i = 3; i < s.size(); i += 5)
	{
		std::string bad = s;
		bad[i] = bad[i] == 'A' ? 'B' : 'A';
		CHECK_EQ(openSecret(bad, k1, out), false);
	}
	CHECK_EQ(openSecret("v2:" + s.substr(3), k1, out), false);
	CHECK_EQ(openSecret("v1:AAAA", k1, out), false);
	CHECK_EQ(openSecret(s, "short", out), false);
	std::string kf = "/tmp/bp-test-key-" + std::to_string(getpid()), key, again;
	unlink(kf.c_str());
	CHECK_EQ(loadOrCreateKey(kf, key), true);
	CHECK_EQ(key.size(), (size_t)32);
	CHECK_EQ(loadOrCreateKey(kf, again), true);
	CHECK_EQ(again == key, true);
	struct stat st;
	CHECK_EQ(stat(kf.c_str(), &st), 0);
	CHECK_EQ((int)(st.st_mode & 0777), 0600);
	unlink(kf.c_str());

	// sidecar
	std::vector<std::string> dir = {"movie.mkv", "movie.srt", "Movie.eng.forced.ASS", "movie - English.srt", "other.srt", "movie.nfo", "movie.idx", "movie.sub"};
	auto subs = subtitleFilesFor("movie.mkv", dir);
	CHECK_EQ(subs.size(), (size_t)5);
	CHECK_EQ(subs[0], std::string("movie.srt"));
	CHECK_EQ(subs[1], std::string("Movie.eng.forced.ASS"));
	CHECK_EQ(subtitleFilesFor("none.mkv", dir).size(), (size_t)0);
	CHECK_EQ(subtitleFilesFor("", dir).size(), (size_t)0);
	CHECK_EQ(coverFileFor({"a.jpg", "Folder.JPG", "cover.png"}), std::string("cover.png"));
	CHECK_EQ(coverFileFor({"x.mp3", ".folder.jpg"}), std::string(".folder.jpg"));
	CHECK_EQ(coverFileFor({"cover.txt", "photo.jpg"}), std::string(""));
}

static void testKeyboard()
{
	auto keys = keyboardLayout(false, ShiftState::Off, 0, 200, 640, 260);
	CHECK_EQ(keys.size(), (size_t)48);
	// the rows tile the width exactly
	for (int r = 0; r < 5; r++)
	{
		int left = 1 << 30, right = 0;
		for (auto& k : keys)
			if (k.row == r) { left = std::min(left, k.x); right = std::max(right, k.x + k.w); }
		CHECK_EQ(left, 0);
		CHECK_EQ(right, 640);
	}
	int q = keyboardHome(keys);
	CHECK_EQ(keys[q].label, std::string("q"));
	// right from q: w; left from q: round to the row's end (-)
	CHECK_EQ(keys[keyboardMove(keys, q, 1, 0)].label, std::string("w"));
	CHECK_EQ(keys[keyboardMove(keys, q, -1, 0)].label, std::string("-"));
	// up from q: 1; up from 1: round to the bottom row's nearest (#+=, under it)
	int one = keyboardMove(keys, q, 0, -1);
	CHECK_EQ(keys[one].label, std::string("1"));
	CHECK_EQ(keys[keyboardMove(keys, one, 0, -1)].label, std::string("#+="));
	// down from b (row 3): Space
	int b = -1;
	for (int i = 0; i < (int)keys.size(); i++)
		if (keys[i].label == "b") b = i;
	CHECK_EQ(keys[keyboardMove(keys, b, 0, 1)].action == KeyAction::Space, true);
	CHECK_EQ(keys[keyboardHit(keys, keys[b].x + 2, keys[b].y + 2)].label, std::string("b"));
	CHECK_EQ(keyboardHit(keys, 5, 100), -1);
	// shifted: capitals, typed as drawn
	auto up = keyboardLayout(false, ShiftState::Once, 0, 200, 640, 260);
	CHECK_EQ(up[keyboardHome(up)].text, std::string("Q"));
	auto sym = keyboardLayout(true, ShiftState::Off, 0, 200, 640, 260);
	CHECK_EQ(sym[11].text, std::string("!"));

	TextEdit t;
	t.set("ab");
	t.insert("c");
	CHECK_EQ(t.text, std::string("abc"));
	t.left();
	t.left();
	t.insert("\xc3\xa9");	// é: two bytes
	CHECK_EQ(t.text, std::string("a\xc3\xa9" "bc"));
	t.backspace();	// the whole é
	CHECK_EQ(t.text, std::string("abc"));
	t.set("x\xc3\xa9");
	t.left();	// before é, not inside it
	CHECK_EQ(t.caret, (size_t)1);
	t.right();
	CHECK_EQ(t.caret, (size_t)3);
	t.backspace();
	t.backspace();
	t.backspace();	// nothing left: nothing happens
	CHECK_EQ(t.text, std::string(""));
}

int main()
{
	testSpeed();
	testSubStyle();
	testSettings();
	testResume();
	testPlaylist();
	testPhoto();
	testUpdate();
	testBacklight();
	testKeyboard();
	testNet();
	testFtpList();
	testBrowser();
	testHistory();
	testBattery();
	testAspect();
	testTime();
	testLabels();
	testTrackList();
	if (failures) fprintf(stderr, "%d failed\n", failures);
	else printf("core: all passed\n");
	return failures != 0;
}
