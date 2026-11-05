#include "AgentSessionManager.h"

NAMESPACE_UPP

static String LStrip(const String& s)
{
	int i = 0;
	while(i < s.GetCount()) {
		char c = s[i];
		if(c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v')
			i++;
		else
			break;
	}
	return s.Mid(i);
}

static bool HasSubagentsComponent(const String& path)
{
	Vector<String> parts = Split(path, '/', false);
	for(int i = 0; i < parts.GetCount(); i++)
		if(parts[i] == "subagents")
			return true;
	return false;
}

static String ParentDirName(const String& path)
{
	String dir = GetFileDirectory(path);
	while(dir.GetCount() > 1 && dir[dir.GetCount() - 1] == '/')
		dir = dir.Left(dir.GetCount() - 1);
	return GetFileName(dir);
}

static void CollectJsonl(const String& dir, Vector<String>& out)
{
	FindFile ff(AppendFileName(dir, "*"));
	if(!ff)
		return;
	do {
		String name = ff.GetName();
		if(ff.IsDirectory()) {
			if(name != "." && name != "..")
				CollectJsonl(AppendFileName(dir, name), out);
		}
		else if(name.EndsWith(".jsonl"))
			out.Add(AppendFileName(dir, name));
	}
	while(ff.Next());
}

static Vector<Value> ReadClaudeJsonl(const String& path)
{
	Vector<Value> records;
	FileIn in(path);
	if(!in)
		return records;
	String all = LoadStream(in);
	Vector<String> lines = Split(all, '\n', false);
	for(int i = 0; i < lines.GetCount(); i++) {
		String line = lines[i];
		if(line.GetCount() > 0 && line[line.GetCount() - 1] == '\r')
			line = line.Left(line.GetCount() - 1);
		Value v = ParseJsonValue(line);
		if(v.IsNull() || !IsValueMap(v))
			continue;
		records.Add(v);
	}
	return records;
}

Vector<SessionEntry> ClaudeEnumerate()
{
	Vector<SessionEntry> out;
	Vector<String> paths = ClaudeJsonlPaths();
	for(int p = 0; p < paths.GetCount(); p++) {
		const String& path = paths[p];
		if(HasSubagentsComponent(path))
			continue;
		Vector<Value> records = ReadClaudeJsonl(path);
		if(records.GetCount() == 0)
			continue;
		String sid;
		for(int i = 0; i < records.GetCount(); i++) {
			ValueMap m = records[i];
			Value v = m.Get("sessionId");
			if(IsString(v) && String(v).GetCount() > 0) {
				sid = String(v);
				break;
			}
		}
		if(sid.IsEmpty())
			sid = GetFileTitle(path);
		String cwd;
		for(int i = 0; i < records.GetCount(); i++) {
			ValueMap m = records[i];
			Value v = m.Get("cwd");
			if(IsString(v) && String(v).GetCount() > 0) {
				cwd = String(v);
				break;
			}
		}
		String title = cwd;
		if(title.IsEmpty())
			title = ParentDirName(path);
		for(int i = 0; i < records.GetCount(); i++) {
			ValueMap m = records[i];
			if(ValueStr(m.Get("type")) != "user")
				continue;
			if(Truthy(m.Get("isMeta")))
				continue;
			Value msg = m.Get("message");
			String text;
			if(IsValueMap(msg)) {
				ValueMap mm = msg;
				text = ContentText(mm.Get("content"));
			}
			if(text.IsEmpty())
				continue;
			String stripped = LStrip(text);
			if(stripped.StartsWith("<local-command-") || stripped.StartsWith("<command-name>"))
				continue;
			String flat;
			flat.Reserve(text.GetCount());
			for(int c = 0; c < text.GetCount(); c++)
				flat.Cat(text[c] == '\n' ? ' ' : text[c]);
			title = ToUtf8(flat.ToWString().Left(90));
			break;
		}
		SessionEntry e;
		e.source = "claude";
		e.id = sid;
		e.title = title;
		e.path = path;
		e.directory = cwd;
		e.updated = 0;
		out.Add(e);
	}
	return out;
}

Vector<MessageEntry> ClaudeMessages(const SessionEntry& s)
{
	Vector<MessageEntry> out;
	Vector<Value> records = ReadClaudeJsonl(s.path);
	for(int i = 0; i < records.GetCount(); i++) {
		ValueMap m = records[i];
		String type = ValueStr(m.Get("type"));
		if(type != "user" && type != "assistant")
			continue;
		if(Truthy(m.Get("isMeta")))
			continue;
		Value msg = m.Get("message");
		ValueMap mm;
		if(IsValueMap(msg))
			mm = ValueMap(msg);
		String text = ContentText(mm.Get("content"));
		if(text.IsEmpty())
			continue;
		String role = ValueStr(mm.Get("role"));
		if(role.IsEmpty())
			role = type;
		MessageEntry e;
		e.role = role;
		e.text = text;
		e.stamp = TimestampText(m.Get("timestamp"));
		out.Add(e);
	}
	return out;
}

SessionState ClaudeState(const SessionEntry& s)
{
	SessionState st;
	String last_role;
	bool last_assistant_running = false;
	Vector<Value> records = ReadClaudeJsonl(s.path);
	for(int i = 0; i < records.GetCount(); i++) {
		ValueMap m = records[i];
		String type = ValueStr(m.Get("type"));
		if((type == "user" || type == "assistant") && !Truthy(m.Get("isMeta"))) {
			String role;
			Value msg = m.Get("message");
			if(IsValueMap(msg)) {
				ValueMap mm = msg;
				role = ValueStr(mm.Get("role"));
			}
			if(role.IsEmpty())
				role = type;
			last_role = role;
		}
		if(type == "assistant") {
			Value msg = m.Get("message");
			ValueMap mm;
			if(IsValueMap(msg))
				mm = ValueMap(msg);
			last_assistant_running = mm.Get("stop_reason").IsNull() && Truthy(mm.Get("content"));
		}
	}
	st.running = last_role == "assistant" && last_assistant_running;
	st.waiting = last_role == "assistant" && !st.running;
	return st;
}

Value ClaudeCursor(const SessionEntry& s)
{
	ValueMap m;
	if(s.path.IsEmpty() || !FileExists(s.path)) {
		m.Add("missing", Value(true));
		return Value(m);
	}
	m.Add("latest", CursorLatestFromFile(s.path));
	return Value(m);
}

Vector<String> ClaudeJsonlPaths()
{
	Vector<String> out;
	CollectJsonl(HomePath(".claude/projects"), out);
	Sort(out);
	return out;
}

Value ClaudeSignature()
{
	ValueArray out;
	Vector<String> paths = ClaudeJsonlPaths();
	for(int i = 0; i < paths.GetCount(); i++) {
		const String& path = paths[i];
		int64 mtime = FileMtimeNs(path);
		int64 size = FileSize64(path);
		ValueArray sig;
		sig.Add(path);
		bool failed = mtime < 0 || size < 0;
		String digest;
		bool hashed = false;
		if(!failed && size <= 8 * 1024 * 1024) {
			FileIn in(path);
			if(in) {
				digest = Sha256Hex(LoadStream(in));
				hashed = true;
			}
			else
				failed = true;
		}
		if(failed) {
			sig.Add(Value());
			sig.Add(Value());
		}
		else {
			sig.Add(mtime);
			sig.Add(size);
			if(hashed)
				sig.Add(digest);
		}
		out.Add(Value(sig));
	}
	return Value(out);
}

END_UPP_NAMESPACE
