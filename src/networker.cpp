#include "networker.h"
#include "netpath.h"
#include "netresolve.h"
#include "playlist.h"
#include <algorithm>
#include <cstdio>

namespace bp
{
	NetWorker::~NetWorker()
	{
		{
			std::lock_guard<std::mutex> g(lock);
			stopping = true;
			jobs.clear();
			if (busy) busy->cancel();
		}
		wake.notify_one();
		if (worker.joinable()) worker.join();
	}

	void NetWorker::list(int serial, const NetServer& server, const std::string& password, const std::string& dir)
	{
		std::lock_guard<std::mutex> g(lock);
		// a newer folder wins: whatever waits for the old one is for nothing
		jobs.clear();
		if (busy) busy->cancel();
		jobs.push_back({serial, server, password, dir, false});
		if (!worker.joinable()) worker = std::thread([this] { run(); });
		wake.notify_one();
	}

	void NetWorker::test(int serial, const NetServer& server, const std::string& password)
	{
		std::lock_guard<std::mutex> g(lock);
		jobs.push_back({serial, server, password, "", true});
		if (!worker.joinable()) worker = std::thread([this] { run(); });
		wake.notify_one();
	}

	void NetWorker::cancelAll()
	{
		std::lock_guard<std::mutex> g(lock);
		jobs.clear();
		if (busy) busy->cancel();
	}

	void NetWorker::forget(const std::string& id)
	{
		std::lock_guard<std::mutex> g(lock);
		forgotten.push_back(id);
	}

	void NetWorker::run()
	{
		std::unique_lock<std::mutex> g(lock);
		for (;;)
		{
			wake.wait(g, [this] { return stopping || !jobs.empty(); });
			if (stopping) return;
			Job job = jobs.front();
			jobs.pop_front();
			for (auto& id : forgotten) backends.erase(id);
			forgotten.clear();
			// a test: a connection of its own, as the form has it now (the kept one is for what's saved). A listing:
			// the server's kept one, made again when its password changed
			std::unique_ptr<NetBackend> testBe;
			NetServer server = job.server;
			bool fresh = job.test || !backends[server.id] || passwordOf[server.id] != job.password;
			g.unlock();
			// (a name asked for off the lock: it can take a second or two)
			if (fresh) server.host = hostAddress(server.host);
			g.lock();
			if (job.test) testBe = makeBackend(server, job.password);
			else if (fresh)
			{
				backends[server.id] = makeBackend(server, job.password);
				passwordOf[server.id] = job.password;
			}
			auto& be = job.test ? testBe : backends[server.id];
			busy = be.get();
			g.unlock();

			auto* out = new NetListing;
			out->serial = job.serial;
			out->dir = job.dir;
			out->test = job.test;
			if (!be) out->result = {NetError::Other, "this kind of server isn't supported here"};
			else if (job.test) out->result = be->connect();
			else
			{
				NetPath np;
				parseNetPath(job.dir, np);
				std::vector<NetEntry> all;
				out->result = be->list(np.path, all);
				// what the browser lists: folders, then what the player plays, each in natural order; hidden (.) names out
				std::vector<NetEntry> dirs, files;
				for (auto& e : all)
				{
					if (!e.dir) out->names.push_back(e.name);
					if (e.name.empty() || e.name[0] == '.') continue;
					if (e.dir) dirs.push_back(e);
					else if (isMediaFile(e.name)) files.push_back(e);
				}
				auto byName = [](const NetEntry& a, const NetEntry& b) { return naturalLess(a.name, b.name); };
				std::sort(dirs.begin(), dirs.end(), byName);
				std::sort(files.begin(), files.end(), byName);
				out->entries = dirs;
				out->entries.insert(out->entries.end(), files.begin(), files.end());
				// the folder's other files (subtitles, covers) stay out of the list: the player has them from names
			}
			if (!out->result.ok()) fprintf(stderr, "net: %s on %s: %s\n", job.test ? "test" : "list", job.server.host.c_str(), out->result.detail.c_str());
			done(out);

			g.lock();
			busy = nullptr;
			testBe.reset();
		}
	}
}
