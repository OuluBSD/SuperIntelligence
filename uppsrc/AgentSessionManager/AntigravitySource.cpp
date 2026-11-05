#include "AgentSessionManager.h"

NAMESPACE_UPP

static bool IsPySpace(char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static String PyStrip(const String& s)
{
	int b = 0;
	int e = s.GetCount();
	while(b < e && IsPySpace(s[b]))
		b++;
	while(e > b && IsPySpace(s[e - 1]))
		e--;
	return s.Mid(b, e - b);
}

static bool IsSpecialOnly(const String& text)
{
	for(int i = 0; i < text.GetCount(); i++) {
		char c = text[i];
		if(!(c == '\"' || c == '$' || c == '-' || (c >= '0' && c <= '9')
		   || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
			return false;
	}
	return true;
}

static bool ReadVarint(const byte *d, int len, int& pos, uint64& value)
{
	value = 0;
	uint64 shift = 0;
	while(pos < len && shift < 70) {
		uint64 b = d[pos];
		pos++;
		if(shift < 64)
			value |= (b & 0x7f) << shift;
		if(b < 0x80)
			return true;
		shift += 7;
	}
	return false;
}

static void ProtobufVarints(const String& data, int depth, Vector<uint64>& out)
{
	if(depth > 8)
		return;
	const byte *d = (const byte *)~data;
	int len = data.GetCount();
	int offset = 0;
	while(offset < len) {
		uint64 key;
		if(!ReadVarint(d, len, offset, key))
			return;
		uint64 wire = key & 7;
		if(wire == 0) {
			uint64 value;
			if(!ReadVarint(d, len, offset, value))
				return;
			out.Add(value);
		}
		else if(wire == 1)
			offset += 8;
		else if(wire == 2) {
			uint64 size;
			if(!ReadVarint(d, len, offset, size))
				return;
			int64 ssize = (int64)size;
			if(ssize < 0 || ssize > len - offset)
				return;
			String child((const char *)(d + offset), (int)ssize);
			offset += (int)ssize;
			ProtobufVarints(child, depth + 1, out);
		}
		else if(wire == 5)
			offset += 4;
		else
			return;
		if(offset > len)
			return;
	}
}

static Value SignatureNull(const String& path)
{
	ValueArray a;
	a.Add(Value(path));
	a.Add(Value());
	a.Add(Value());
	return Value(a);
}

static Value FileSignature(const String& path, int64 hash_limit)
{
	int64 mtime = FileMtimeNs(path);
	int64 size = FileSize64(path);
	if(mtime < 0 || size < 0)
		return SignatureNull(path);
	ValueArray a;
	a.Add(Value(path));
	a.Add(Value(mtime));
	a.Add(Value(size));
	if(size <= hash_limit) {
		FileIn in(path);
		if(!in)
			return SignatureNull(path);
		String data = LoadStream(in);
		if(in.IsError())
			return SignatureNull(path);
		a.Add(Value(Sha256Hex(data)));
	}
	return Value(a);
}

static String AntigravityRoot()
{
	return AppendFileName(AppendFileName(GetHomeDirectory(), ".gemini"), "antigravity-cli");
}

Value AntigravitySignature()
{
	String root = AntigravityRoot();
	ValueArray result;
	result.Add(FileSignature(AppendFileName(AppendFileName(root, "cache"), "conversation_metadata.json"), 8 * 1024 * 1024));
	result.Add(FileSignature(AppendFileName(root, "conversation_summaries.db"), 0));
	String dir = AppendFileName(root, "conversations");
	Vector<String> paths;
	if(DirectoryExists(dir)) {
		FindFile ff(AppendFileName(dir, "*.db"));
		while(ff) {
			if(ff.IsFile())
				paths.Add(ff.GetPath());
			ff.Next();
		}
		Sort(paths);
	}
	for(int i = 0; i < paths.GetCount(); i++)
		result.Add(FileSignature(paths[i], 0));
	return Value(result);
}

Vector<SessionEntry> AntigravityEnumerate()
{
	Vector<SessionEntry> out;
	String root = AntigravityRoot();
	String summaries = AppendFileName(root, "conversation_summaries.db");
	if(FileExists(summaries)) {
		SqliteRo ro;
		if(!ro.Open(summaries))
			throw SessionError(ro.Error());
		SqliteStatement q;
		if(!q.Prepare(ro, "SELECT conversation_id,title,preview,workspace_uris,last_modified_time"
		                  " FROM conversation_summaries"
		                  " WHERE parent_conversation_id='' AND nesting_depth=0"
		                  " ORDER BY last_modified_time DESC"))
			throw SessionError(q.Error());
		for(;;) {
			SqlStepResult r = q.Step();
			if(r == SQLITE_STEP_DONE)
				break;
			if(r == SQLITE_STEP_ERROR)
				throw SessionError(q.Error());
			String sid = q.Text(0);
			String title = q.Text(1);
			String preview = q.Text(2);
			String directory;
			Value dirs_v = ParseJsonValue(q.Text(3));
			if(IsValueArray(dirs_v)) {
				ValueArray dirs = dirs_v;
				if(dirs.GetCount() > 0)
					directory = StripPrefix(ValueStr(dirs[0]), "file://");
			}
			String title_used = preview.GetCount() ? preview : (title.GetCount() ? title : sid);
			SessionEntry& e = out.Add();
			e.source = "antigravity";
			e.id = sid;
			e.title = title_used;
			e.path = AppendFileName(AppendFileName(root, "conversations"), sid + ".db");
			e.directory = directory;
			e.updated = IsoParseMs(q.Text(4));
		}
		return out;
	}
	Value root_v = ParseJsonValue(LoadFile(AppendFileName(AppendFileName(root, "cache"), "conversation_metadata.json")));
	ValueMap cm;
	if(IsValueMap(root_v)) {
		ValueMap rm = root_v;
		Value cv = rm["conversations"];
		if(IsValueMap(cv))
			cm = cv;
	}
	for(int i = 0; i < cm.GetCount(); i++) {
		String sid = cm.GetKey(i);
		Value rv = cm.GetValue(i);
		if(!IsValueMap(rv))
			continue;
		ValueMap record = rv;
		Value internal = record["is_internal"];
		bool truthy = false;
		if(internal.Is<bool>())
			truthy = (bool)internal;
		else if(IsNumber(internal))
			truthy = (double)internal != 0;
		if(truthy)
			continue;
		ValueMap summary;
		Value sv = record["summary"];
		if(IsValueMap(sv))
			summary = sv;
		String directory;
		Value wsv = summary["WorkspaceURIs"];
		if(IsValueArray(wsv)) {
			ValueArray workspaces = wsv;
			if(workspaces.GetCount() > 0)
				directory = StripPrefix(ValueStr(workspaces[0]), "file://");
		}
		String title = ValueStr(summary["Preview"]);
		if(title.IsEmpty())
			title = ValueStr(summary["Title"]);
		if(title.IsEmpty())
			title = sid;
		SessionEntry& e = out.Add();
		e.source = "antigravity";
		e.id = sid;
		e.title = title;
		e.path = AppendFileName(AppendFileName(root, "conversations"), sid + ".db");
		e.directory = directory;
		e.updated = 0;
	}
	return out;
}

Vector<MessageEntry> AntigravityMessages(const SessionEntry& s)
{
	Vector<MessageEntry> out;
	if(s.path.IsEmpty() || !FileExists(s.path))
		return out;
	SqliteRo ro;
	if(!ro.Open(s.path))
		throw SessionError(ro.Error());
	SqliteStatement q;
	if(!q.Prepare(ro, "SELECT idx,step_type,status,step_payload FROM steps ORDER BY idx"))
		throw SessionError(q.Error());
	for(;;) {
		SqlStepResult r = q.Step();
		if(r == SQLITE_STEP_DONE)
			break;
		if(r == SQLITE_STEP_ERROR)
			throw SessionError(q.Error());
		int64 idx = q.Int(0);
		int64 step_type = q.Int(1);
		int64 status = q.Int(2);
		String payload = q.Blob(3);
		Vector<String> strings;
		const byte *d = (const byte *)~payload;
		int len = payload.GetCount();
		int i = 0;
		while(i < len) {
			if(d[i] < 0x20 || d[i] > 0x7e) {
				i++;
				continue;
			}
			int j = i;
			while(j < len && d[j] >= 0x20 && d[j] <= 0x7e)
				j++;
			if(j - i >= 4) {
				String text = PyStrip(String((const char *)(d + i), j - i));
				if(!text.IsEmpty() && FindIndex(strings, text) < 0 && !text.StartsWith("$")
				   && !text.StartsWith("b$") && !text.StartsWith("sessionID")
				   && !IsSpecialOnly(text))
					strings.Add(text);
			}
			i = j;
		}
		if(strings.GetCount()) {
			Vector<uint64> varints;
			ProtobufVarints(payload, 0, varints);
			uint64 stampvalue = 0;
			bool have = false;
			for(int k = 0; k < varints.GetCount(); k++) {
				uint64 v = varints[k];
				if(v >= 1500000000ULL && v <= 4000000000ULL && (!have || v < stampvalue)) {
					stampvalue = v;
					have = true;
				}
			}
			MessageEntry& e = out.Add();
			e.role = Format("Step %lld (type %lld, status %lld)", idx + 1, step_type, status);
			for(int k = 0; k < strings.GetCount(); k++) {
				if(k)
					e.text << "\n\n";
				e.text << strings[k];
			}
			e.stamp = have ? TimestampText(Value((int64)stampvalue)) : String();
		}
	}
	return out;
}

SessionState AntigravityState(const SessionEntry& s)
{
	SessionState st;
	if(s.path.IsEmpty() || !FileExists(s.path))
		return st;
	SqliteRo ro;
	if(!ro.Open(s.path))
		throw SessionError(ro.Error());
	SqliteStatement q;
	if(!q.Prepare(ro, "SELECT step_type,status FROM steps ORDER BY idx DESC LIMIT 1"))
		throw SessionError(q.Error());
	SqlStepResult r = q.Step();
	if(r == SQLITE_STEP_ERROR)
		throw SessionError(q.Error());
	if(r == SQLITE_STEP_ROW) {
		int64 status = q.Int(1);
		st.running = status != 3;
		st.waiting = !st.running && (q.Int(0) == 17 || q.Int(0) == 98);
	}
	return st;
}

Value AntigravityCursor(const SessionEntry& s)
{
	ValueMap result;
	if(s.path.IsEmpty() || !FileExists(s.path)) {
		result.Add("missing", Value(true));
		return Value(result);
	}
	SqliteRo ro;
	if(!ro.Open(s.path))
		throw SessionError(ro.Error());
	SqliteStatement q;
	if(!q.Prepare(ro, "SELECT idx,status FROM steps ORDER BY idx DESC LIMIT 1"))
		throw SessionError(q.Error());
	SqlStepResult r = q.Step();
	if(r == SQLITE_STEP_ERROR)
		throw SessionError(q.Error());
	if(r == SQLITE_STEP_ROW) {
		ValueArray pair;
		pair.Add(Value(q.Int(0)));
		pair.Add(Value(q.Int(1)));
		result.Add("latest_step", Value(pair));
	}
	else
		result.Add("latest_step", Value());
	return Value(result);
}

END_UPP_NAMESPACE
