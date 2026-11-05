#include "AgentSessionManager.h"

NAMESPACE_UPP

static bool SameValue(const Value& a, const Value& b)
{
	if(a.IsNull() || b.IsNull())
		return a.IsNull() && b.IsNull();
	if(a.Is<bool>() || b.Is<bool>())
		return a.Is<bool>() && b.Is<bool>() && (bool)a == (bool)b;
	if(IsNumber(a) || IsNumber(b)) {
		if(!IsNumber(a) || !IsNumber(b))
			return false;
		if(a.Is<int>() && b.Is<int>())
			return (int)a == (int)b;
		if(a.Is<int64>() && b.Is<int64>())
			return (int64)a == (int64)b;
		return (double)a == (double)b;
	}
	if(IsString(a) || IsString(b))
		return IsString(a) && IsString(b) && String(a) == String(b);
	if(a.GetType() == VALUEMAP_V && b.GetType() == VALUEMAP_V) {
		ValueMap ma = a;
		ValueMap mb = b;
		if(ma.GetCount() != mb.GetCount())
			return false;
		for(int i = 0; i < ma.GetCount(); i++) {
			Value k = ma.GetKey(i);
			int q = -1;
			for(int j = 0; j < mb.GetCount(); j++) {
				Value kb = mb.GetKey(j);
				if(IsString(k) && IsString(kb) && String(kb) == String(k)) {
					q = j;
					break;
				}
				if(k.Is<int>() && kb.Is<int>() && (int)kb == (int)k) {
					q = j;
					break;
				}
			}
			if(q < 0)
				return false;
			if(!SameValue(ma.GetValue(i), mb.GetValue(q)))
				return false;
		}
		return true;
	}
	if(a.GetType() == VALUEARRAY_V && b.GetType() == VALUEARRAY_V) {
		ValueArray aa = a;
		ValueArray ab = b;
		if(aa.GetCount() != ab.GetCount())
			return false;
		for(int i = 0; i < aa.GetCount(); i++)
			if(!SameValue(aa[i], ab[i]))
				return false;
		return true;
	}
	return false;
}

static Value SourceSignature(const String& source)
{
	if(source == "claude")
		return ClaudeSignature();
	if(source == "kilo")
		return KiloSignature();
	if(source == "antigravity")
		return AntigravitySignature();
	ValueMap m;
	m.Add("claude", ClaudeSignature());
	m.Add("kilo", KiloSignature());
	m.Add("antigravity", AntigravitySignature());
	return Value(m);
}

static String CacheFilePath()
{
	return AppendFileName(HomePath(".cache/session-tool"), "sessions.json");
}

static SessionEntry SessionFromCacheRecord(const Value& v)
{
	SessionEntry e;
	if(!IsValueMap(v))
		return e;
	ValueMap m = v;
	e.source = ValueStr(m.Get("source"));
	e.id = ValueStr(m.Get("id"));
	e.title = ValueStr(m.Get("title"));
	e.path = ValueStr(m.Get("path"));
	e.directory = ValueStr(m.Get("directory"));
	e.updated = TimestampValueToMs(m.Get("updated"));
	if(e.updated < 0)
		e.updated = 0;
	return e;
}

Vector<SessionEntry> CachedSessions(const String& source)
{
	Value signature = SourceSignature(source);
	String cachefile = CacheFilePath();
	ValueMap cache;
	Value loaded = ParseJsonValue(LoadFile(cachefile));
	if(IsValueMap(loaded))
		cache = ValueMap(loaded);
	ValueMap cached;
	Value cachedv = cache.Get(source);
	if(IsValueMap(cachedv))
		cached = ValueMap(cachedv);
	Value cached_sig = cached.Get("signature");
	Value cached_at = cached.Get("cached_at");
	if(!cached_sig.IsNull() && SameValue(cached_sig, signature)
	   && !cached_at.IsNull() && IsNumber(cached_at)) {
		double now = (double)::time(NULL);
		if(now - (double)cached_at < 300) {
			Vector<SessionEntry> out;
			Value sv = cached.Get("sessions");
			if(IsValueArray(sv)) {
				ValueArray sa = sv;
				for(int i = 0; i < sa.GetCount(); i++) {
					SessionEntry e = SessionFromCacheRecord(sa[i]);
					if(!e.id.IsEmpty() || !e.source.IsEmpty())
						out.Add(e);
				}
			}
			return out;
		}
	}
	Vector<SessionEntry> sessions = AllSessions(source);
	ValueArray records;
	for(int i = 0; i < sessions.GetCount(); i++) {
		const SessionEntry& e = sessions[i];
		ValueMap r;
		r.Add("source", Value(e.source));
		r.Add("id", Value(e.id));
		r.Add("title", Value(e.title));
		r.Add("path", e.path.IsEmpty() ? Value() : Value(e.path));
		r.Add("directory", Value(e.directory));
		r.Add("updated", Value(e.updated));
		records.Add(Value(r));
	}
	ValueMap fresh;
	fresh.Add("signature", signature);
	fresh.Add("cached_at", Value((double)::time(NULL)));
	fresh.Add("sessions", Value(records));
	cache.RemoveKey(source);
	cache.Add(source, Value(fresh));
	String tmp = AppendFileName(GetFileDirectory(cachefile), "sessions.tmp");
	{
		FileOut out(tmp);
		if(out) {
			out.Put(DumpJson(Value(cache), false, false));
			out.Close();
			if(::rename(~tmp, ~cachefile) != 0)
				FileDelete(tmp);
		}
	}
	return sessions;
}

Vector<SessionEntry> AllSessions(const String& source)
{
	Vector<SessionEntry> out;
	if(source == "claude" || source == "all") {
		Vector<SessionEntry> s = ClaudeEnumerate();
		out.Append(s);
	}
	if(source == "kilo" || source == "all") {
		Vector<SessionEntry> s = KiloEnumerate();
		out.Append(s);
	}
	if(source == "antigravity" || source == "all") {
		Vector<SessionEntry> s = AntigravityEnumerate();
		out.Append(s);
	}
	return out;
}

SessionEntry FindSession(const String& source, const String& id)
{
	Vector<SessionEntry> matches;
	Vector<SessionEntry> sessions = AllSessions(source);
	for(int i = 0; i < sessions.GetCount(); i++)
		if(sessions[i].id == id || sessions[i].id.StartsWith(id))
			matches.Add(sessions[i]);
	if(matches.GetCount() != 1)
		throw SessionError(matches.IsEmpty() ? "session not found"
			: "session ID is ambiguous; use a longer ID");
	return matches[0];
}

Value SessionCursor(const SessionEntry& s)
{
	if(s.source == "claude")
		return ClaudeCursor(s);
	if(s.source == "kilo")
		return KiloCursor(s);
	return AntigravityCursor(s);
}

static String B64UrlEncode(const String& raw)
{
	String b64 = Base64Encode(raw);
	String out;
	out.Reserve(b64.GetCount());
	for(int i = 0; i < b64.GetCount(); i++) {
		char c = b64[i];
		if(c == '=')
			break;
		out.Cat(c == '+' ? '-' : (c == '/' ? '_' : c));
	}
	return out;
}

static String B64UrlDecode(const String& tok)
{
	String std;
	std.Reserve(tok.GetCount());
	for(int i = 0; i < tok.GetCount(); i++) {
		char c = tok[i];
		std.Cat(c == '-' ? '+' : (c == '_' ? '/' : c));
	}
	int pad = (4 - std.GetLength() % 4) % 4;
	for(int i = 0; i < pad; i++)
		std << '=';
	return Base64Decode(std);
}

String EncodeCookie(const SessionEntry& s)
{
	ValueMap value;
	value.Add("cursor", SessionCursor(s));
	value.Add("session", Value(s.id));
	value.Add("source", Value(s.source));
	value.Add("version", Value(1));
	String raw = DumpJson(Value(value), false, true, true);
	return B64UrlEncode(raw);
}

bool CheckCookie(const SessionEntry& s, const String& cookie)
{
	if(cookie.IsEmpty())
		return false;
	String raw = B64UrlDecode(cookie);
	Value previous = ParseJsonValue(raw);
	if(!IsValueMap(previous))
		throw SessionError("invalid cookie; pass the cookie returned by the previous changes check");
	ValueMap pv = previous;
	Value version = pv.Get("version");
	if(version.IsNull() || (double)version != 1.0)
		throw SessionError("cookie belongs to a different session or is unsupported");
	if(ValueStr(pv.Get("source")) != s.source || ValueStr(pv.Get("session")) != s.id)
		throw SessionError("cookie belongs to a different session or is unsupported");
	Value current_cursor = SessionCursor(s);
	Value previous_cursor = pv.Get("cursor");
	return !SameValue(previous_cursor, current_cursor);
}

END_UPP_NAMESPACE
