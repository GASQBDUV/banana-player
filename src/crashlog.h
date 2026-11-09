// A crash's record in <state dir>/crash.log, kept across runs (log.txt keeps only the last two runs' output).
// The handler (SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT) runs on its own stack and uses only async-signal-safe
// calls: everything it can is formatted at install(). The file never passes CAP: install() trims it to its newest
// entries once it could no longer take a whole one, and the handler writes at most ENTRY_MAX, never past CAP.
#pragma once
#include <cstddef>
#include <string>

namespace bp {
namespace crashlog {

constexpr size_t CAP = 64 * 1024;	// crash.log's most
constexpr size_t ENTRY_MAX = 16 * 1024;	// one crash's most
constexpr size_t KEEP = 32 * 1024;	// what a trim keeps (the newest entries)
constexpr int MAX_FRAMES = 64;
constexpr size_t FRAME_LINE_MAX = 200;	// a backtrace line's most (C++ names can be long)
// each entry starts with this, at a line's start: where a trim may cut
constexpr const char* MARK = "=== crash ";

// Installs the handlers (and std::terminate's, which records an uncaught exception's message before abort) and trims
// the file. stateDir empty: no file, only the line on stderr.
void install(const std::string& stateDir, const char* version, const char* commit);
// What was playing, shown in the record. From the main thread; the handler reads the last one set.
void setContext(const char* text);
// BP_CRASH_TEST=segv|abort|throw|overflow: crashes on purpose (a test hook)
void testCrash(const char* how);

// The pure parts, unit-tested
// text's newest part of at most keep bytes, from an entry's start ("" if no entry starts in it)
std::string trimmed(const std::string& text, size_t keep);
// trims the file at path to trimmed(…, keep) through a temporary file and a rename, when it's over limit
// (true: it was trimmed)
bool trimFile(const std::string& path, size_t limit, size_t keep);
// "2026-11-09 14:03:12 +0700": epoch seconds shown at a UTC offset, into out (no allocation: the handler's)
size_t formatTime(char* out, size_t n, long long epoch, long offsetSec);

}
}
