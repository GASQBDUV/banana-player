#include "netstore.h"
#include "netpath.h"
#include "secret.h"
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace bp
{
	std::string netErrorText(NetError e, const std::string& host)
	{
		switch (e)
		{
		case NetError::NoNetwork: return "No network. Is Wi-Fi on?";
		case NetError::NotFound: return "Can't find " + host;
		case NetError::Auth: return "Wrong user or password";
		case NetError::NoAccess: return "No access here";
		case NetError::Timeout: return host + " isn't answering";
		case NetError::Gone: return "The connection was lost";
		default: return "This folder can't be opened";
		}
	}

	void NetStore::load(const std::string& stateDir)
	{
		dir = stateDir;
		list.clear();
		key.clear();
		if (dir.empty()) return;
		std::string fileKey;
		if (loadOrCreateKey(dir + "/servers.key", fileKey)) key = deriveKey(fileKey, deviceSerial());
		std::ifstream in(dir + "/servers.conf");
		std::stringstream s;
		s << in.rdbuf();
		list = parseServers(s.str());
	}

	const NetServer* NetStore::find(const std::string& id) const
	{
		for (auto& s : list)
			if (s.id == id) return &s;
		return nullptr;
	}

	std::string NetStore::put(NetServer s, const std::string& pw, bool keepPassword)
	{
		if (s.id.empty()) s.id = newServerId();
		auto it = std::find_if(list.begin(), list.end(), [&](const NetServer& o) { return o.id == s.id; });
		if (keepPassword && pw.empty() && it != list.end()) s.secret = it->secret;
		else s.secret = s.guest || pw.empty() || key.empty() ? "" : sealSecret(pw, key);
		if (it != list.end()) *it = s;
		else list.push_back(s);
		changes++;
		save();
		return s.id;
	}

	void NetStore::remove(const std::string& id)
	{
		list.erase(std::remove_if(list.begin(), list.end(), [&](const NetServer& s) { return s.id == id; }), list.end());
		changes++;
		save();
	}

	void NetStore::touch(const std::string& id)
	{
		for (auto& s : list)
			if (s.id == id) s.lastUsed = (long long)time(nullptr);
		save();
	}

	std::string NetStore::password(const NetServer& s) const
	{
		std::string plain;
		if (s.guest || s.secret.empty() || key.empty() || !openSecret(s.secret, key, plain)) return "";
		return plain;
	}

	void NetStore::save() const
	{
		if (dir.empty()) return;
		// written beside and renamed over, readable by its owner only (the passwords are sealed, the rest needn't be
		// seen either)
		std::string path = dir + "/servers.conf", tmp = path + ".tmp";
		{
			std::ofstream out(tmp, std::ios::trunc);
			out << serializeServers(list);
			if (!out.flush()) return;
		}
		chmod(tmp.c_str(), 0600);
		rename(tmp.c_str(), path.c_str());
	}
}
