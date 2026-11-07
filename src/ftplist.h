// What an FTP server answers to a listing, as entries: MLSD (RFC 3659, facts then a name) and the older LIST, whose
// format is whatever the server's `ls` gives (Unix style) or IIS gives (DOS style).
#pragma once
#include "net.h"
#include <string>
#include <vector>

namespace bp
{
	// "." and ".." (and a MLSD's cdir/pdir) are left out, a line it can't read too
	std::vector<NetEntry> parseMlsd(const std::string& text);
	std::vector<NetEntry> parseFtpList(const std::string& text);
}
