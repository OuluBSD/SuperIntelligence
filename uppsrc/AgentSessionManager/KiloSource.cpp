#include "AgentSessionManager.h"

NAMESPACE_UPP

static String KiloDbPath()
{
	return AppendFileName(HomePath(".local/share/kilo"), "kilo.db");
}

Value KiloSignature()
{
	ValueArray out;
	String paths[2];
	paths[0] = KiloDbPath();
	paths[1] = AppendFileName(GetFileDirectory(paths[0]), "kilo.db-wal");
	for(int i = 0; i < 2; i++) {
		const String& path = paths[i];
		int64 mtime = FileMtimeNs(path);
		int64 size = FileSize64(path);
		ValueArray sig;
		sig.Add(path);
		if(mtime < 0 || size < 0) {
			sig.Add(Value());
			sig.Add(Value());
		}
		else {
			sig.Add(mtime / 300000000000LL);
			sig.Add(size);
		}
		out.Add(Value(sig));
	}
	return Value(out);
}

Vector<SessionEntry> KiloEnumerate()
{
	Vector<SessionEntry> out;
	String db = KiloDbPath();
	if(!FileExists(db))
		return out;
	SqliteRo ro;
	if(!ro.Open(db))
		throw SessionError(ro.Error());
	SqliteStatement st;
	const char *sql = "SELECT id,title,agent,directory,time_updated FROM session ORDER BY time_updated DESC";
	if(!st.Prepare(ro, sql))
		throw SessionError(ro.Error() + ": " + sql);
	SqlStepResult r = st.Step();
	while(r == SQLITE_STEP_ROW) {
		SessionEntry e;
		e.source = "kilo";
		e.id = st.Text(0);
		e.title = st.Text(1);
		if(e.title.IsEmpty())
			e.title = "(untitled)";
		e.directory = st.Text(3);
		e.updated = st.Int(4);
		out.Add(e);
		r = st.Step();
	}
	if(r == SQLITE_STEP_ERROR)
		throw SessionError(ro.Error());
	return out;
}

Vector<MessageEntry> KiloMessages(const SessionEntry& s)
{
	Vector<MessageEntry> out;
	String db = KiloDbPath();
	if(!FileExists(db))
		return out;
	SqliteRo ro;
	if(!ro.Open(db))
		throw SessionError(ro.Error());
	SqliteStatement st;
	const char *sql = "SELECT id,data,time_created FROM message WHERE session_id=? ORDER BY time_created,id";
	if(!st.Prepare(ro, sql))
		throw SessionError(ro.Error() + ": " + sql);
	st.BindText(1, s.id);
	SqlStepResult r = st.Step();
	while(r == SQLITE_STEP_ROW) {
		String message_id = st.Text(0);
		String data = st.Text(1);
		int64 created = st.Int(2);
		Value msgv = ParseJsonValue(data);
		if(msgv.IsNull() || !IsValueMap(msgv))
			throw SessionError("invalid message data");
		ValueMap msg = ValueMap(msgv);
		String role = ValueStr(msg.Get("role"));
		if(role.IsEmpty())
			role = "message";
		Vector<String> pieces;
		SqliteStatement ps;
		const char *psql = "SELECT data FROM part WHERE message_id=? ORDER BY time_created,id";
		if(!ps.Prepare(ro, psql))
			throw SessionError(ro.Error() + ": " + psql);
		ps.BindText(1, message_id);
		SqlStepResult pr = ps.Step();
		while(pr == SQLITE_STEP_ROW) {
			Value partv = ParseJsonValue(ps.Text(0));
			if(partv.IsNull() || !IsValueMap(partv))
				throw SessionError("invalid part data");
			ValueMap part = ValueMap(partv);
			String kind = ValueStr(part.Get("type"));
			if(kind == "text") {
				Value t = part.Get("text");
				if(IsString(t) && String(t).GetCount() > 0)
					pieces.Add(String(t));
			}
			else if(kind == "tool") {
				Value statev = part.Get("state");
				ValueMap state;
				if(IsValueMap(statev))
					state = ValueMap(statev);
				String toolname = ValueStr(part.Get("tool"));
				if(toolname.IsEmpty())
					toolname = "tool";
				String status = ValueStr(state.Get("status"));
				if(status.IsEmpty())
					status = "unknown";
				pieces.Add("**Tool: " + toolname + " (" + status + ")**");
				Value inp = state.Get("input");
				if(Truthy(inp))
					pieces.Add("```json\n" + DumpJson(inp, true, false) + "\n```");
				Value outp = state.Get("output");
				if(Truthy(outp)) {
					if(IsString(outp))
						pieces.Add(String(outp));
					else
						pieces.Add(DumpJson(outp, false, false));
				}
			}
			else if(kind == "reasoning") {
				Value t = part.Get("text");
				if(IsString(t) && String(t).GetCount() > 0)
					pieces.Add("<details><summary>Reasoning</summary>\n\n" + String(t) + "\n</details>");
			}
			pr = ps.Step();
		}
		if(pr == SQLITE_STEP_ERROR)
			throw SessionError(ro.Error());
		if(pieces.GetCount() > 0) {
			Value tmap = msg.Get("time");
			Value created_v;
			if(IsValueMap(tmap))
				created_v = ValueMap(tmap).Get("created");
			if(!Truthy(created_v))
				created_v = Value(created);
			MessageEntry e;
			e.role = role;
			for(int i = 0; i < pieces.GetCount(); i++) {
				if(i)
					e.text << "\n\n";
				e.text << pieces[i];
			}
			e.stamp = TimestampText(created_v);
			out.Add(e);
		}
		r = st.Step();
	}
	if(r == SQLITE_STEP_ERROR)
		throw SessionError(ro.Error());
	return out;
}

SessionState KiloState(const SessionEntry& s)
{
	SessionState out;
	String db = KiloDbPath();
	if(!FileExists(db))
		return out;
	SqliteRo ro;
	if(!ro.Open(db))
		throw SessionError(ro.Error());
	SqliteStatement st;
	const char *sql = "SELECT id,data FROM message WHERE session_id=? ORDER BY time_created DESC,id DESC LIMIT 1";
	if(!st.Prepare(ro, sql))
		throw SessionError(ro.Error() + ": " + sql);
	st.BindText(1, s.id);
	SqlStepResult r = st.Step();
	if(r == SQLITE_STEP_ERROR)
		throw SessionError(ro.Error());
	if(r != SQLITE_STEP_ROW)
		return out;
	String message_id = st.Text(0);
	String raw = st.Text(1);
	Value msgv = ParseJsonValue(raw);
	if(msgv.IsNull() || !IsValueMap(msgv))
		throw SessionError("invalid message data");
	ValueMap msg = ValueMap(msgv);
	String role = ValueStr(msg.Get("role"));
	if(role != "assistant")
		return out;
	Value tmap = msg.Get("time");
	if(IsValueMap(tmap) && Truthy(ValueMap(tmap).Get("completed"))) {
		out.running = false;
		out.waiting = true;
		return out;
	}
	SqliteStatement ps;
	const char *psql = "SELECT data FROM part WHERE message_id=?";
	if(!ps.Prepare(ro, psql))
		throw SessionError(ro.Error() + ": " + psql);
	ps.BindText(1, message_id);
	SqlStepResult pr = ps.Step();
	while(pr == SQLITE_STEP_ROW) {
		Value partv = ParseJsonValue(ps.Text(0));
		if(partv.IsNull() || !IsValueMap(partv)) {
			pr = ps.Step();
			continue;
		}
		ValueMap part = ValueMap(partv);
		if(ValueStr(part.Get("type")) == "tool") {
			Value statev = part.Get("state");
			if(IsValueMap(statev) && ValueStr(ValueMap(statev).Get("status")) == "running") {
				out.running = true;
				out.waiting = false;
				return out;
			}
		}
		pr = ps.Step();
	}
	if(pr == SQLITE_STEP_ERROR)
		throw SessionError(ro.Error());
	int64 updated = s.updated;
	bool recent = updated != 0 && (NowMs() - updated) < 300000;
	out.running = recent;
	out.waiting = false;
	return out;
}

Value KiloCursor(const SessionEntry& s)
{
	ValueMap result;
	String db = KiloDbPath();
	if(!FileExists(db)) {
		result.Add("missing", Value(true));
		return Value(result);
	}
	SqliteRo ro;
	if(!ro.Open(db))
		throw SessionError(ro.Error());
	SqliteStatement st;
	const char *sql = "SELECT id FROM message WHERE session_id=? ORDER BY time_created DESC,id DESC LIMIT 1";
	if(!st.Prepare(ro, sql))
		throw SessionError(ro.Error() + ": " + sql);
	st.BindText(1, s.id);
	SqlStepResult r = st.Step();
	if(r == SQLITE_STEP_ERROR)
		throw SessionError(ro.Error());
	if(r == SQLITE_STEP_ROW)
		result.Add("latest_message", Value(st.Text(0)));
	else
		result.Add("latest_message", Value());
	return Value(result);
}

END_UPP_NAMESPACE
