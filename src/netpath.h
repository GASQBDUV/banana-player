// bpnet://<server-id>/<path>: a place on a saved network server, as a plain string.
#pragma once
#include <string>

namespace bp
{
	struct NetPath
	{
		std::string server;
		std::string path;	// decoded, starts with "/"; "/" is the server's root (an SMB server's share list)
	};

	bool isNetPath(const std::string& s);
	bool parseNetPath(const std::string& s, NetPath& out);
	// Tidies path (single slashes, "." and ".." resolved, never above "/") and encodes it.
	std::string makeNetPath(const std::string& server, const std::string& path);
	std::string netParent(const std::string& s);	// the root's parent is the root
	std::string netBaseName(const std::string& s);	// "" for the root
	std::string netJoin(const std::string& dir, const std::string& name);

	// Only '%', '?', '#' and control characters are encoded: the rest stays readable and mpv-safe.
	std::string netEncode(const std::string& s);
	std::string netDecode(const std::string& s);

	std::string newServerId();	// 8 lowercase hex characters
}
