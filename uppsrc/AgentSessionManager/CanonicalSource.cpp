#include "AgentSessionManager.h"

NAMESPACE_UPP

CanonicalSession ClaudeToCanonical(const SessionEntry& s)
{
	CanonicalSession out;
	out.id = s.id;
	out.source = "claude";
	out.title = s.title;
	out.directory = s.directory;
	ValueMap meta;
	meta.Add("file", Value(s.path));
	int skipped = 0;
	bool have = false;
	int64 first_ts = 0;
	int64 last_ts = 0;
	FileIn in(s.path);
	if(in) {
		String all = LoadStream(in);
		Vector<String> lines = Split(all, '\n', false);
		for(int i = 0; i < lines.GetCount(); i++) {
			String line = lines[i];
			if(line.GetCount() > 0 && line[line.GetCount() - 1] == '\r')
				line = line.Left(line.GetCount() - 1);
			Value v = ParseJsonValue(line);
			if(v.IsNull() || !IsValueMap(v)) {
				skipped++;
				continue;
			}
			ValueMap m = v;
			int64 ts = TimestampValueToMs(m.Get("timestamp"));
			if(!have) {
				first_ts = ts;
				have = true;
			}
			last_ts = ts;
			String type = ValueStr(m.Get("type"));
			if((type != "user" && type != "assistant") || Truthy(m.Get("isMeta"))) {
				skipped++;
				continue;
			}
			CanonicalMessage& msg = out.messages.Add();
			msg.id = ValueStr(m.Get("uuid"));
			msg.stamp_ms = ts;
			msg.src_json = line;
			Value msgv = m.Get("message");
			if(IsValueMap(msgv)) {
				ValueMap mm = msgv;
				Value content = mm.Get("content");
				if(IsString(content)) {
					CanonicalPart& p = msg.parts.Add();
					p.type = "text";
					p.text = String(content);
				}
				else if(IsValueArray(content)) {
					ValueArray ca = content;
					for(int c = 0; c < ca.GetCount(); c++) {
						const Value& block = ca[c];
						CanonicalPart& p = msg.parts.Add();
						if(!IsValueMap(block)) {
							ValueMap pmeta;
							pmeta.Add("src_block", Value(DumpJson(block, false, false)));
							p.meta = pmeta;
							continue;
						}
						ValueMap bm = block;
						String btype = ValueStr(bm.Get("type"));
						if(btype == "text") {
							p.type = "text";
							p.text = ValueStr(bm.Get("text"));
						}
						else if(btype == "tool_use") {
							p.type = "tool_use";
							p.tool = ValueStr(bm.Get("name"));
							p.call_id = ValueStr(bm.Get("id"));
							Value input = bm.Get("input");
							if(!input.IsNull())
								p.input_json = DumpJson(input, false, false);
						}
						else if(btype == "tool_result") {
							p.type = "tool_result";
							p.call_id = ValueStr(bm.Get("tool_use_id"));
							Value cv = bm.Get("content");
							if(IsString(cv))
								p.text = String(cv);
							else if(!cv.IsNull()) {
								ValueMap pmeta;
								pmeta.Add("content_json", Value(DumpJson(cv, false, false)));
								p.meta = pmeta;
							}
						}
						else if(btype == "thinking") {
							p.type = "reasoning";
							p.text = ValueStr(bm.Get("thinking"));
							if(p.text.IsEmpty())
								p.text = ValueStr(bm.Get("text"));
						}
						else {
							p.type = btype;
							ValueMap pmeta;
							pmeta.Add("src_block", Value(DumpJson(block, false, false)));
							p.meta = pmeta;
						}
					}
				}
				String role = ValueStr(mm.Get("role"));
				if(role.IsEmpty())
					role = type;
				msg.role = role;
			}
			else
				msg.role = type;
		}
	}
	if(have) {
		out.created_ms = first_ts;
		out.updated_ms = last_ts;
	}
	meta.Add("skipped", Value(skipped));
	out.meta = meta;
	return out;
}

CanonicalSession KiloToCanonical(const SessionEntry& s)
{
	String db = AppendFileName(HomePath(".local/share/kilo"), "kilo.db");
	if(!FileExists(db))
		throw SessionError("kilo database not found");
	SqliteRo ro;
	if(!ro.Open(db))
		throw SessionError(ro.Error());
	SqliteStatement st;
	const char *sql = "SELECT id,title,agent,directory,slug,version,cost,tokens_input,"
	                  "tokens_output,tokens_reasoning,tokens_cache_read,tokens_cache_write,"
	                  "summary_additions,summary_deletions,summary_files,time_created,time_updated,"
	                  "model"
	                  " FROM session WHERE id=?";
	if(!st.Prepare(ro, sql))
		throw SessionError(ro.Error() + ": " + sql);
	st.BindText(1, s.id);
	SqlStepResult r = st.Step();
	if(r == SQLITE_STEP_ERROR)
		throw SessionError(ro.Error());
	if(r != SQLITE_STEP_ROW)
		throw SessionError("kilo session not found");
	CanonicalSession out;
	out.id = st.Text(0);
	out.source = "kilo";
	out.title = st.Text(1);
	out.directory = st.Text(3);
	out.created_ms = st.Int(15);
	out.updated_ms = st.Int(16);
	ValueMap meta;
	if(!st.IsNull(2))
		meta.Add("agent", Value(st.Text(2)));
	if(!st.IsNull(4))
		meta.Add("slug", Value(st.Text(4)));
	if(!st.IsNull(5))
		meta.Add("version", Value(st.Text(5)));
	if(!st.IsNull(6))
		meta.Add("cost", Value(st.Int(6)));
	if(!st.IsNull(7))
		meta.Add("tokens_input", Value(st.Int(7)));
	if(!st.IsNull(8))
		meta.Add("tokens_output", Value(st.Int(8)));
	if(!st.IsNull(9))
		meta.Add("tokens_reasoning", Value(st.Int(9)));
	if(!st.IsNull(10))
		meta.Add("tokens_cache_read", Value(st.Int(10)));
	if(!st.IsNull(11))
		meta.Add("tokens_cache_write", Value(st.Int(11)));
	if(!st.IsNull(12))
		meta.Add("summary_additions", Value(st.Int(12)));
	if(!st.IsNull(13))
		meta.Add("summary_deletions", Value(st.Int(13)));
	if(!st.IsNull(14))
		meta.Add("summary_files", Value(st.Int(14)));
	if(!st.IsNull(17)) {
		Value mv = ParseJsonValue(st.Text(17));
		if(!mv.IsNull())
			meta.Add("model", mv);
		else
			meta.Add("model", Value(st.Text(17)));
	}
	out.meta = meta;
	SqliteStatement ms;
	const char *msql = "SELECT id,data,time_created FROM message WHERE session_id=? ORDER BY time_created,id";
	if(!ms.Prepare(ro, msql))
		throw SessionError(ro.Error() + ": " + msql);
	ms.BindText(1, s.id);
	SqlStepResult mr = ms.Step();
	while(mr == SQLITE_STEP_ROW) {
		CanonicalMessage& msg = out.messages.Add();
		msg.id = ms.Text(0);
		msg.src_json = ms.Text(1);
		int64 stamp = ms.Int(2);
		String role = "message";
		Value msgv = ParseJsonValue(msg.src_json);
		if(IsValueMap(msgv)) {
			ValueMap mm = msgv;
			role = ValueStr(mm.Get("role"));
			if(role.IsEmpty())
				role = "message";
			Value tmap = mm.Get("time");
			if(IsValueMap(tmap)) {
				Value cv = ValueMap(tmap).Get("created");
				if(IsNumber(cv))
					stamp = (int64)(double)cv;
			}
		}
		msg.role = role;
		msg.stamp_ms = stamp;
		SqliteStatement ps;
		const char *psql = "SELECT id,data,time_created FROM part WHERE message_id=? ORDER BY time_created,id";
		if(!ps.Prepare(ro, psql))
			throw SessionError(ro.Error() + ": " + psql);
		ps.BindText(1, msg.id);
		SqlStepResult pr = ps.Step();
		while(pr == SQLITE_STEP_ROW) {
			CanonicalPart& part = msg.parts.Add();
			part.id = ps.Text(0);
			part.stamp_ms = ps.Int(2);
			part.src_json = ps.Text(1);
			Value partv = ParseJsonValue(part.src_json);
			if(IsValueMap(partv)) {
				ValueMap pm = partv;
				String ptype = ValueStr(pm.Get("type"));
				if(ptype == "text" || ptype == "reasoning") {
					part.type = ptype;
					part.text = ValueStr(pm.Get("text"));
				}
				else if(ptype == "tool") {
					part.type = "tool_use";
					part.tool = ValueStr(pm.Get("tool"));
					part.call_id = ValueStr(pm.Get("callID"));
					Value statev = pm.Get("state");
					if(IsValueMap(statev)) {
						ValueMap state = ValueMap(statev);
						part.status = ValueStr(state.Get("status"));
						Value inp = state.Get("input");
						if(!inp.IsNull())
							part.input_json = DumpJson(inp, false, false);
						Value outp = state.Get("output");
						if(IsString(outp))
							part.output_json = String(outp);
						else if(!outp.IsNull())
							part.output_json = DumpJson(outp, false, false);
					}
					if(part.status.IsEmpty())
						part.status = "unknown";
				}
				else {
					part.type = ptype;
					part.text = ValueStr(pm.Get("text"));
				}
			}
			pr = ps.Step();
		}
		if(pr == SQLITE_STEP_ERROR)
			throw SessionError(ro.Error());
		mr = ms.Step();
	}
	if(mr == SQLITE_STEP_ERROR)
		throw SessionError(ro.Error());
	if(out.created_ms == 0 && out.messages.GetCount() > 0)
		out.created_ms = out.messages[0].stamp_ms;
	return out;
}

static bool CanonicalIsPySpace(char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static String CanonicalPyStrip(const String& s)
{
	int b = 0;
	int e = s.GetCount();
	while(b < e && CanonicalIsPySpace(s[b]))
		b++;
	while(e > b && CanonicalIsPySpace(s[e - 1]))
		e--;
	return s.Mid(b, e - b);
}

static bool CanonicalIsSpecialOnly(const String& text)
{
	for(int i = 0; i < text.GetCount(); i++) {
		char c = text[i];
		if(!(c == '\"' || c == '$' || c == '-' || (c >= '0' && c <= '9')
		   || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
			return false;
	}
	return true;
}

static bool CanonicalReadVarint(const byte *d, int len, int& pos, uint64& value)
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

static void CanonicalProtobufVarints(const String& data, int depth, Vector<uint64>& out)
{
	if(depth > 8)
		return;
	const byte *d = (const byte *)~data;
	int len = data.GetCount();
	int offset = 0;
	while(offset < len) {
		uint64 key;
		if(!CanonicalReadVarint(d, len, offset, key))
			return;
		uint64 wire = key & 7;
		if(wire == 0) {
			uint64 value;
			if(!CanonicalReadVarint(d, len, offset, value))
				return;
			out.Add(value);
		}
		else if(wire == 1)
			offset += 8;
		else if(wire == 2) {
			uint64 size;
			if(!CanonicalReadVarint(d, len, offset, size))
				return;
			int64 ssize = (int64)size;
			if(ssize < 0 || ssize > len - offset)
				return;
			String child((const char *)(d + offset), (int)ssize);
			offset += (int)ssize;
			CanonicalProtobufVarints(child, depth + 1, out);
		}
		else if(wire == 5)
			offset += 4;
		else
			return;
		if(offset > len)
			return;
	}
}

CanonicalSession AntigravityToCanonical(const SessionEntry& s)
{
	if(s.path.IsEmpty() || !FileExists(s.path))
		throw SessionError("antigravity conversation database not found");
	SqliteRo ro;
	if(!ro.Open(s.path))
		throw SessionError(ro.Error());
	SqliteStatement q;
	if(!q.Prepare(ro, "SELECT idx,step_type,status,step_payload FROM steps ORDER BY idx"))
		throw SessionError(q.Error());
	CanonicalSession out;
	out.id = s.id;
	out.source = "antigravity";
	out.title = s.title;
	out.directory = s.directory;
	ValueMap meta;
	meta.Add("file", Value(s.path));
	out.meta = meta;
	bool have = false;
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
				String text = CanonicalPyStrip(String((const char *)(d + i), j - i));
				if(!text.IsEmpty() && FindIndex(strings, text) < 0 && !text.StartsWith("$")
				   && !text.StartsWith("b$") && !text.StartsWith("sessionID")
				   && !CanonicalIsSpecialOnly(text))
					strings.Add(text);
			}
			i = j;
		}
		Vector<uint64> varints;
		CanonicalProtobufVarints(payload, 0, varints);
		uint64 stampvalue = 0;
		bool stamped = false;
		for(int k = 0; k < varints.GetCount(); k++) {
			uint64 v = varints[k];
			if(v >= 1500000000ULL && v <= 4000000000ULL && (!stamped || v < stampvalue)) {
				stampvalue = v;
				stamped = true;
			}
		}
		int64 stamp = stamped ? (int64)stampvalue * 1000 : 0;
		if(!have) {
			out.created_ms = stamp;
			have = true;
		}
		out.updated_ms = stamp;
		CanonicalMessage& msg = out.messages.Add();
		msg.id = Format("step-%lld", idx);
		msg.role = step_type == 14 ? "user" : "assistant";
		msg.stamp_ms = stamp;
		CanonicalPart& part = msg.parts.Add();
		part.type = "step";
		part.stamp_ms = stamp;
		for(int k = 0; k < strings.GetCount(); k++) {
			if(k)
				part.text << "\n\n";
			part.text << strings[k];
		}
		ValueMap pmeta;
		pmeta.Add("idx", Value(idx));
		pmeta.Add("step_type", Value(step_type));
		pmeta.Add("status", Value(status));
		part.meta = pmeta;
	}
	return out;
}

CanonicalSession ToCanonical(const SessionEntry& s)
{
	if(s.source == "claude")
		return ClaudeToCanonical(s);
	if(s.source == "kilo")
		return KiloToCanonical(s);
	if(s.source == "antigravity")
		return AntigravityToCanonical(s);
	throw SessionError("unknown source: " + s.source);
}

END_UPP_NAMESPACE
