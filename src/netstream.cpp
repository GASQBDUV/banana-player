#include "netstream.h"
#include "netpath.h"
#include "netresolve.h"
#include <atomic>
#include <cstdio>
#include <mpv/client.h>
#include <mpv/stream_cb.h>

namespace bp
{
	struct NetStreams::Stream
	{
		NetStreams* owner;
		std::string uri;
		std::unique_ptr<NetBackend> backend;
		std::unique_ptr<NetFile> file;
		int64_t pos = 0;
		// mpv's cancel is for the reads to come too (the file's own is for the one waiting)
		std::atomic<bool> cancelled{false};
	};

	void NetStreams::update(const NetStore& store)
	{
		std::lock_guard<std::mutex> g(lock);
		servers.clear();
		for (auto& s : store.servers()) servers[s.id] = {s, store.password(s)};
	}

	bool NetStreams::attach(mpv_handle* mpv)
	{
		return mpv_stream_cb_add_ro(mpv, "bpnet", this, &NetStreams::open) >= 0;
	}

	NetResult NetStreams::takeFailure(const std::string& uri)
	{
		std::lock_guard<std::mutex> g(lock);
		auto it = failures.find(uri);
		if (it == failures.end()) return {};
		NetResult r = it->second;
		failures.erase(it);
		return r;
	}

	NetResult NetStreams::failure(const std::string& uri)
	{
		std::lock_guard<std::mutex> g(lock);
		auto it = failures.find(uri);
		return it == failures.end() ? NetResult() : it->second;
	}

	void NetStreams::cancelOpens()
	{
		std::lock_guard<std::mutex> g(lock);
		for (auto* b : opening) b->cancel();
	}

	void NetStreams::fail(const std::string& uri, const NetResult& r)
	{
		if (r.error == NetError::Cancelled) return;	// given up on purpose: not the file's fault
		fprintf(stderr, "net: %s: %s\n", netBaseName(uri).c_str(), r.detail.c_str());
		std::lock_guard<std::mutex> g(lock);
		failures[uri] = r;
	}

	int NetStreams::open(void* self, char* uri, mpv_stream_cb_info* info)
	{
		auto* me = (NetStreams*)self;
		NetPath np;
		if (!parseNetPath(uri, np)) return MPV_ERROR_LOADING_FAILED;
		NetServer server;
		std::string password;
		{
			std::lock_guard<std::mutex> g(me->lock);
			me->failures.erase(uri);
			auto it = me->servers.find(np.server);
			if (it == me->servers.end())
			{
				me->failures[uri] = {NetError::NotFound, "no saved server " + np.server};
				return MPV_ERROR_LOADING_FAILED;
			}
			server = it->second.first;
			password = it->second.second;
		}
		auto* s = new Stream;
		s->owner = me;
		s->uri = uri;
		server.host = hostAddress(server.host);
		s->backend = makeBackend(server, password);
		if (!s->backend)
		{
			me->fail(uri, {NetError::Other, "this kind of server isn't supported here"});
			delete s;
			return MPV_ERROR_LOADING_FAILED;
		}
		{
			std::lock_guard<std::mutex> g(me->lock);
			me->opening.insert(s->backend.get());
		}
		NetResult r;
		s->file = s->backend->open(np.path, r);
		{
			std::lock_guard<std::mutex> g(me->lock);
			me->opening.erase(s->backend.get());
		}
		if (!s->file)
		{
			me->fail(uri, r);
			delete s;
			return MPV_ERROR_LOADING_FAILED;
		}
		info->cookie = s;
		info->read_fn = &NetStreams::read;
		info->seek_fn = &NetStreams::seek;
		info->size_fn = &NetStreams::size;
		info->close_fn = &NetStreams::close;
		info->cancel_fn = &NetStreams::cancel;
		return 0;
	}

	int64_t NetStreams::read(void* cookie, char* buf, uint64_t n)
	{
		auto* s = (Stream*)cookie;
		if (s->cancelled) return -1;
		int64_t got = s->file->read(buf, (int64_t)n, s->pos);
		if (got < 0)
		{
			// mpv takes it as the file's end: why is kept for the player
			if (!s->cancelled) s->owner->fail(s->uri, s->file->lastResult());
			return -1;
		}
		s->pos += got;
		return got;
	}

	int64_t NetStreams::seek(void* cookie, int64_t offset)
	{
		auto* s = (Stream*)cookie;
		if (offset < 0) return MPV_ERROR_GENERIC;
		s->pos = offset;	// read where it's asked: no request until then
		return offset;
	}

	int64_t NetStreams::size(void* cookie)
	{
		int64_t n = ((Stream*)cookie)->file->size();
		return n < 0 ? MPV_ERROR_UNSUPPORTED : n;
	}

	void NetStreams::close(void* cookie)
	{
		delete (Stream*)cookie;
	}

	void NetStreams::cancel(void* cookie)
	{
		auto* s = (Stream*)cookie;
		s->cancelled = true;
		s->file->cancel();
	}
}
