#ifndef _AgentSessionManager_SessionModel_h_
#define _AgentSessionManager_SessionModel_h_

#include <Core/Core.h>

NAMESPACE_UPP

struct SessionEntry : Moveable<SessionEntry> {
	String source;
	String id;
	String title;
	String path;
	String directory;
	int64  updated;
};

struct MessageEntry : Moveable<MessageEntry> {
	String role;
	String text;
	String stamp;
};

struct SessionState {
	bool running = false;
	bool waiting = false;
};

struct SessionError {
	String message;
	SessionError(const String& m) : message(m) {}
};

String IsoTimestampMs(int64 epoch_ms);
int64  IsoParseMs(const String& s);
int64  TimestampValueToMs(const Value& v);
String TimestampText(const Value& v);
String ContentText(const Value& content);
String DumpJson(const Value& v, bool pretty, bool ensure_ascii, bool nospace = false);
String JsonEscape(const String& s, bool ensure_ascii);
Value  ParseJsonValue(const String& text);
String ValueStr(const Value& v);
bool   Truthy(const Value& v);
String HomePath(const char *rel);
String Sha256Hex(const String& data);
int64  FileMtimeNs(const String& path);
int64  FileSize64(const String& path);
String Capitalize(const String& s);
String StripPrefix(const String& s, const String& prefix);
bool   ParseArgsInt(const String& s, int& out);
Value  CursorLatestFromFile(const String& path);
int64  NowMs();

END_UPP_NAMESPACE

#endif
