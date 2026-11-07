// The saved network connections, as the player keeps them: servers.conf in the state folder, each password sealed
// (secret.h) with a key made from a random key file beside it and the handheld's serial: a copy of the folder taken
// over BananaOS's own network share opens nothing.
#pragma once
#include "net.h"
#include "servers.h"
#include <string>
#include <vector>

namespace bp
{
	// what went wrong, as the panel says it ("Can't find MY-PC"); host: the server's name or address
	std::string netErrorText(NetError e, const std::string& host);

	class NetStore
	{
	public:
		// stateDir "": nothing kept (a test run)
		void load(const std::string& stateDir);
		const std::vector<NetServer>& servers() const { return list; }
		const NetServer* find(const std::string& id) const;
		// added (a new id) or changed (the same id); password "" with keepPassword: the one it had. Returns its id
		std::string put(NetServer s, const std::string& password, bool keepPassword = false);
		void remove(const std::string& id);
		void touch(const std::string& id);	// lastUsed now
		int version() const { return changes; }	// changes with each server added, changed or removed
		// the server's password, opened; "" for a guest or one that won't open (another handheld's copy)
		std::string password(const NetServer& s) const;

	private:
		void save() const;
		std::string dir, key;
		int changes = 0;
		std::vector<NetServer> list;
	};
}
