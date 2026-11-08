// Saved playlists: extended M3U files in the state folder, and the edits the browser makes on them.
// No window or mpv here; tests/core_test.cpp covers it.
#pragma once
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace bp
{
	struct PlaylistEntry
	{
		std::string path, title;	// path: absolute local, or bpnet://<id>/<path> (never credentials)
		double duration = -1;
	};

	struct Playlist
	{
		std::string file, name;	// file (its full path) is the playlist's id
		std::vector<PlaylistEntry> entries;
	};

	const size_t PL_MAX_FILE_BYTES = 1024 * 1024;
	const size_t PL_MAX_LINE_BYTES = 4096;
	const size_t PL_MAX_ENTRIES = 2000;
	const size_t PL_MAX_LISTED = 500;
	const size_t PL_NAME_MAX_BYTES = 40;

	// Reads out.file's stem as the name when there's no #PLAYLIST line (set out.file first). `skipped` counts what
	// was dropped: photos, non-media, other URL schemes, over-long lines, entries past the cap. False: too big.
	bool parseM3u(const std::string& text, const std::string& m3uDir, Playlist& out, int& skipped);
	std::string serializeM3u(const Playlist&);
	std::string entryTitle(const std::string& path);	// file stem (netBaseName for bpnet)

	enum class PlNameError { None, Empty, TooLong, BadChar, Taken };
	// trimmed, 1-40 bytes, no control chars and no '/', unique ignoring ASCII case; renaming may keep its own name
	PlNameError checkPlaylistName(const std::string&, const std::vector<std::string>& existing, const std::string& renaming = "");
	std::string playlistFileName(const std::string& name);	// sanitised stem + ".m3u", no clash handling

	// Editing; each returns remap: old index -> new index, -1 = removed.
	std::vector<int> addEntries(std::vector<PlaylistEntry>&, const std::vector<PlaylistEntry>& add, int at /*-1 end*/, size_t cap, int& dropped);
	std::vector<int> removeEntries(std::vector<PlaylistEntry>&, const std::set<int>& sel);
	std::vector<int> moveSelection(std::vector<PlaylistEntry>&, const std::set<int>& sel, int dir /*-1 up, +1 down*/);
	std::vector<int> gatherSelection(std::vector<PlaylistEntry>&, const std::set<int>& sel);	// block at the first selected's place, order kept
	std::vector<int> removeDuplicates(std::vector<PlaylistEntry>&, int& removed);	// keeps each path's first
	std::set<int> remapSelection(const std::set<int>&, const std::vector<int>& remap);
	// A removed one gives the next survivor's new index; with none after it, the last survivor's; -1 if none survive.
	int remapIndex(int i, const std::vector<int>& remap);
	std::vector<int> shuffleOrder(int n, int first, uint64_t seed);	// a permutation with `first` first (any, if out of range)
	// dot names skipped; isVideoFile||isAudioFile kept; photos counted; others ignored; naturalLess; netJoin for bpnet
	std::vector<PlaylistEntry> folderEntries(const std::string& dir, const std::vector<std::string>& names, int& skippedPhotos);

	bool writeFileAtomic(const std::string& path, const std::string& text);	// tmp + rename; false on failure

	class PlaylistStore
	{
	public:
		explicit PlaylistStore(std::string dir);	// created by mkdir on first save
		struct Info { std::string file, name; int count; };
		std::vector<Info> list() const;	// naturalLess by name
		bool load(const std::string& file, Playlist&, int& skipped) const;
		bool save(const Playlist&);
		PlNameError create(const std::string& name, std::string& fileOut);
		PlNameError rename(const std::string& file, const std::string& newName, std::string& newFileOut);	// new written, old unlinked
		bool remove(const std::string& file);
		int serial() const { return serial_; }	// bumped by create/rename/remove/save
		const std::string& dir() const { return dir_; }

	private:
		std::string uniquePath(const std::string& name, const std::string& allowSame) const;
		std::string dir_;
		int serial_ = 0;
	};
}
