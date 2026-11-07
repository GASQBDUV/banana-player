// bpnet:// files as mpv streams (libmpv's stream_cb): mpv's demuxer thread opens and reads them, each with a backend
// of its own (a connection per file: libsmb2's contexts are one thread's). The saved servers come as a copy (the
// browser changes the NetStore on the main thread). A read that fails is an end of file to mpv: why it failed is kept
// here, for the player to tell a lost connection from a file played to its end.
#pragma once
#include "net.h"
#include "netstore.h"
#include <map>
#include <mutex>
#include <set>
#include <string>

struct mpv_handle;
struct mpv_stream_cb_info;

namespace bp
{
	class NetStreams
	{
	public:
		// the servers and their passwords, as they are now (after each change of the store)
		void update(const NetStore& store);
		// mpv opens bpnet:// through this from now on
		bool attach(mpv_handle* mpv);
		// what failed reading or opening the file (a bpnet path) since it was last asked; None if nothing did
		NetResult takeFailure(const std::string& uri);
		NetResult failure(const std::string& uri);	// the same, kept
		// opens still connecting are given up (another file picked, the player closed): mpv waits on them
		void cancelOpens();

	private:
		struct Stream;
		static int open(void* self, char* uri, ::mpv_stream_cb_info* info);
		static int64_t read(void* cookie, char* buf, uint64_t n);
		static int64_t seek(void* cookie, int64_t offset);
		static int64_t size(void* cookie);
		static void close(void* cookie);
		static void cancel(void* cookie);
		void fail(const std::string& uri, const NetResult& r);

		std::mutex lock;
		std::map<std::string, std::pair<NetServer, std::string>> servers;	// id: the server, its password
		std::map<std::string, NetResult> failures;
		std::set<NetBackend*> opening;
	};
}
