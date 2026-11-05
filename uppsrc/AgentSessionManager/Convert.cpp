#include "AgentSessionManager.h"

#include <sys/wait.h>

NAMESPACE_UPP

static const char *KiloPartTypes[] = {
	"text", "subtask", "reasoning", "file", "tool",
	"step-start", "step-finish", "snapshot", "patch", "agent", "retry", "compaction"
};

static bool IsKiloPartType(const String& t)
{
	for(int i = 0; i < 12; i++)
		if(t == KiloPartTypes[i])
			return true;
	return false;
}

static String SlugifyTitle(const String& title)
{
	String out;
	for(int i = 0; i < title.GetCount(); i++) {
		char c = title[i];
		if(IsAlNum(c))
			out.Cat(ToLower(c));
		else if(!out.IsEmpty() && out[out.GetCount() - 1] != '-')
			out.Cat('-');
	}
	while(out.GetCount() && out[out.GetCount() - 1] == '-')
		out = out.Left(out.GetCount() - 1);
	if(out.IsEmpty())
		out = "converted";
	return out;
}

static ValueMap CopyMapSkipping(const ValueMap& src, const char *skip1, const char *skip2, const char *skip3)
{
	ValueMap out;
	for(int i = 0; i < src.GetCount(); i++) {
		Value k = src.GetKey(i);
		String ks = IsString(k) ? String(k) : k.ToString();
		if((skip1 && ks == skip1) || (skip2 && ks == skip2) || (skip3 && ks == skip3))
			continue;
		out.Add(k, src.GetValue(i));
	}
	return out;
}

static Value PartOutputText(const CanonicalPart& p)
{
	if(!p.output_json.IsEmpty()) {
		Value ov = ParseJsonValue(p.output_json);
		if(IsString(ov))
			return ov;
		if(ov.IsNull())
			return Value(p.output_json);
		return Value(DumpJson(ov, false, false));
	}
	if(p.type == "tool_result" && !p.text.IsEmpty())
		return Value(p.text);
	return Value(String());
}

static Value KiloToolState(const CanonicalPart& p, const CanonicalMessage& msg, const CanonicalSession& s,
	const Value& resolved_output)
{
	ValueMap state;
	int64 start = p.stamp_ms > 0 ? p.stamp_ms : (msg.stamp_ms > 0 ? msg.stamp_ms : s.updated_ms);
	int64 end = start;
	ValueMap input;
	if(!p.input_json.IsEmpty()) {
		Value iv = ParseJsonValue(p.input_json);
		if(IsValueMap(iv))
			input = ValueMap(iv);
	}
	if(p.status == "error") {
		state.Add("status", Value("error"));
		state.Add("input", Value(input));
		state.Add("error", IsNull(resolved_output) ? Value(String()) : resolved_output);
		state.Add("metadata", Value(ValueMap()));
		ValueMap t;
		t.Add("start", Value(start));
		t.Add("end", Value(end));
		state.Add("time", Value(t));
		return Value(state);
	}
	state.Add("status", Value("completed"));
	state.Add("input", Value(input));
	state.Add("output", IsNull(resolved_output) ? Value(String()) : resolved_output);
	state.Add("title", Value(p.tool.IsEmpty() ? String("tool") : p.tool));
	state.Add("metadata", Value(ValueMap()));
	ValueMap t;
	t.Add("start", Value(start));
	t.Add("end", Value(end));
	state.Add("time", Value(t));
	return Value(state);
}

static Value KiloPartJson(const CanonicalPart& p, const CanonicalMessage& msg, const CanonicalSession& s,
	const String& sid, const String& mid, const VectorMap<String, Value>& result_text_by_call)
{
	Value src = ParseJsonValue(p.src_json);
	if(IsValueMap(src) && IsKiloPartType(ValueStr(ValueMap(src).Get("type")))) {
		ValueMap out;
		out.Add("id", Value(GenKiloId("prt")));
		out.Add("sessionID", Value(sid));
		out.Add("messageID", Value(mid));
		ValueMap rest = CopyMapSkipping(ValueMap(src), "id", "sessionID", "messageID");
		for(int i = 0; i < rest.GetCount(); i++)
			out.Add(rest.GetKey(i), rest.GetValue(i));
		return Value(out);
	}
	String pid = GenKiloId("prt");
	if(p.type == "tool_use") {
		Value outv;
		if(!p.output_json.IsEmpty())
			outv = PartOutputText(p);
		else
			outv = result_text_by_call.Get(p.call_id, Value(String()));
		ValueMap m;
		m.Add("id", Value(pid));
		m.Add("sessionID", Value(sid));
		m.Add("messageID", Value(mid));
		m.Add("type", Value("tool"));
		m.Add("callID", Value(p.call_id.IsEmpty() ? GenCallId() : p.call_id));
		m.Add("tool", Value(p.tool.IsEmpty() ? String("tool") : p.tool));
		m.Add("state", KiloToolState(p, msg, s, outv));
		return Value(m);
	}
	if(p.type == "reasoning") {
		ValueMap m;
		m.Add("id", Value(pid));
		m.Add("sessionID", Value(sid));
		m.Add("messageID", Value(mid));
		m.Add("type", Value("reasoning"));
		m.Add("text", Value(p.text));
		int64 t = p.stamp_ms > 0 ? p.stamp_ms : (msg.stamp_ms > 0 ? msg.stamp_ms : s.updated_ms);
		ValueMap tm;
		tm.Add("start", Value(t));
		m.Add("time", Value(tm));
		return Value(m);
	}
	ValueMap m;
	m.Add("id", Value(pid));
	m.Add("sessionID", Value(sid));
	m.Add("messageID", Value(mid));
	m.Add("type", Value("text"));
	String text = p.text;
	if(p.type == "tool_result") {
		Value rt = result_text_by_call.Get(p.call_id, Value());
		if(!rt.IsNull())
			text = String(rt);
	}
	m.Add("text", Value(text));
	return Value(m);
}

static Value KiloMessageInfo(const CanonicalMessage& msg, const CanonicalSession& s, const String& sid,
	const String& mid, const String& prev_mid)
{
	Value src = ParseJsonValue(msg.src_json);
	if(s.source == "kilo" && IsValueMap(src)) {
		ValueMap out;
		out.Add("id", Value(mid));
		out.Add("sessionID", Value(sid));
		ValueMap rest = CopyMapSkipping(ValueMap(src), "id", "sessionID", NULL);
		for(int i = 0; i < rest.GetCount(); i++)
			out.Add(rest.GetKey(i), rest.GetValue(i));
		int64 stamp = msg.stamp_ms > 0 ? msg.stamp_ms : s.updated_ms;
		if(ValueStr(rest.Get("role")) == "assistant") {
			if(rest.Get("parentID").IsNull())
				out.Add("parentID", Value(prev_mid.IsEmpty() ? mid : prev_mid));
			if(rest.Get("modelID").IsNull())
				out.Add("modelID", Value(String("unknown")));
			if(rest.Get("providerID").IsNull())
				out.Add("providerID", Value(String("unknown")));
			if(rest.Get("mode").IsNull())
				out.Add("mode", Value(String("build")));
			if(rest.Get("agent").IsNull())
				out.Add("agent", Value(String("code")));
			if(rest.Get("path").IsNull()) {
				ValueMap path;
				path.Add("cwd", Value(s.directory));
				path.Add("root", Value(s.directory));
				out.Add("path", Value(path));
			}
			if(rest.Get("cost").IsNull())
				out.Add("cost", Value((double)0.0));
			if(rest.Get("tokens").IsNull()) {
				ValueMap tokens;
				tokens.Add("input", Value((int64)0));
				tokens.Add("output", Value((int64)0));
				tokens.Add("reasoning", Value((int64)0));
				ValueMap cache;
				cache.Add("read", Value((int64)0));
				cache.Add("write", Value((int64)0));
				tokens.Add("cache", Value(cache));
				out.Add("tokens", Value(tokens));
			}
		} else {
			if(rest.Get("agent").IsNull())
				out.Add("agent", Value(String("code")));
			if(rest.Get("model").IsNull()) {
				ValueMap model;
				model.Add("providerID", Value(String("unknown")));
				model.Add("modelID", Value(String("unknown")));
				out.Add("model", Value(model));
			}
			if(rest.Get("time").IsNull()) {
				ValueMap tm;
				tm.Add("created", Value(stamp));
				out.Add("time", Value(tm));
			}
		}
		return Value(out);
	}
	String role = msg.role == "assistant" ? "assistant" : "user";
	String agent = "code";
	Value mmeta = ValueMap(s.meta).Get("agent");
	if(IsString(mmeta) && !String(mmeta).IsEmpty())
		agent = String(mmeta);
	ValueMap model;
	model.Add("providerID", Value(String("unknown")));
	model.Add("modelID", Value(String("unknown")));
	ValueMap m;
	m.Add("id", Value(mid));
	m.Add("sessionID", Value(sid));
	m.Add("role", Value(role));
	ValueMap tm;
	tm.Add("created", Value(msg.stamp_ms > 0 ? msg.stamp_ms : s.updated_ms));
	if(role == "assistant")
		tm.Add("completed", Value(msg.stamp_ms > 0 ? msg.stamp_ms : s.updated_ms));
	m.Add("time", Value(tm));
	if(role == "assistant") {
		m.Add("parentID", Value(prev_mid.IsEmpty() ? mid : prev_mid));
		m.Add("modelID", Value(String("unknown")));
		m.Add("providerID", Value(String("unknown")));
		m.Add("mode", Value(String("build")));
		m.Add("agent", Value(agent));
		ValueMap path;
		path.Add("cwd", Value(s.directory));
		path.Add("root", Value(s.directory));
		m.Add("path", Value(path));
		m.Add("cost", Value((double)0.0));
		ValueMap tokens;
		tokens.Add("input", Value((int64)0));
		tokens.Add("output", Value((int64)0));
		tokens.Add("reasoning", Value((int64)0));
		ValueMap cache;
		cache.Add("read", Value((int64)0));
		cache.Add("write", Value((int64)0));
		tokens.Add("cache", Value(cache));
		m.Add("tokens", Value(tokens));
	} else {
		m.Add("agent", Value(agent));
		m.Add("model", Value(model));
	}
	return Value(m);
}

Value ToKiloExportJson(const CanonicalSession& s, bool fresh_session_id, String& error)
{
	error.Clear();
	String sid = s.id;
	if(fresh_session_id || !sid.StartsWith("ses"))
		sid = GenKiloId("ses");
	String title = s.title + " [from " + s.source + " " + s.id + "]";
	ValueMap info;
	info.Add("id", Value(sid));
	Value mslug = ValueMap(s.meta).Get("slug");
	info.Add("slug", Value(IsString(mslug) && !String(mslug).IsEmpty()
		? String(mslug) : SlugifyTitle(s.title)));
	info.Add("projectID", Value(GenHexId(40)));
	info.Add("directory", Value(s.directory));
	info.Add("path", Value(String("")));
	info.Add("title", Value(title));
	Value mversion = ValueMap(s.meta).Get("version");
	info.Add("version", Value(IsString(mversion) && !String(mversion).IsEmpty()
		? String(mversion) : String("converted-1")));
	Value magent = ValueMap(s.meta).Get("agent");
	if(IsString(magent) && !String(magent).IsEmpty())
		info.Add("agent", magent);
	Value mmodel = ValueMap(s.meta).Get("model");
	if(IsValueMap(mmodel))
		info.Add("model", mmodel);
	ValueMap time;
	time.Add("created", Value(s.created_ms > 0 ? s.created_ms : s.updated_ms));
	time.Add("updated", Value(s.updated_ms > 0 ? s.updated_ms : s.created_ms));
	info.Add("time", Value(time));
	Value mcost = ValueMap(s.meta).Get("cost");
	if(IsNumber(mcost))
		info.Add("cost", mcost);
	Value mtok = ValueMap(s.meta).Get("tokens_input");
	if(IsNumber(mtok)) {
		ValueMap tokens;
		tokens.Add("input", mtok);
		tokens.Add("output", ValueMap(s.meta).Get("tokens_output"));
		tokens.Add("reasoning", ValueMap(s.meta).Get("tokens_reasoning"));
		ValueMap cache;
		cache.Add("read", ValueMap(s.meta).Get("tokens_cache_read"));
		cache.Add("write", ValueMap(s.meta).Get("tokens_cache_write"));
		tokens.Add("cache", Value(cache));
		info.Add("tokens", Value(tokens));
	}
	Value msum = ValueMap(s.meta).Get("summary_additions");
	if(IsNumber(msum)) {
		ValueMap summary;
		summary.Add("additions", msum);
		summary.Add("deletions", ValueMap(s.meta).Get("summary_deletions"));
		summary.Add("files", ValueMap(s.meta).Get("summary_files"));
		info.Add("summary", Value(summary));
	}
	VectorMap<String, Value> result_text_by_call;
	ValueArray messages;
	String prev_mid;
	for(int i = 0; i < s.messages.GetCount(); i++) {
		const CanonicalMessage& msg = s.messages[i];
		for(int j = 0; j < msg.parts.GetCount(); j++) {
			const CanonicalPart& p = msg.parts[j];
			if(p.type == "tool_result" && !p.call_id.IsEmpty() && result_text_by_call.Find(p.call_id) < 0) {
				Value t = PartOutputText(p);
				if(!IsNull(t))
					result_text_by_call.GetAdd(p.call_id, t);
			}
		}
	}
	for(int i = 0; i < s.messages.GetCount(); i++) {
		const CanonicalMessage& msg = s.messages[i];
		String mid = GenKiloId("msg");
		ValueMap entry;
		entry.Add("info", KiloMessageInfo(msg, s, sid, mid, prev_mid));
		ValueArray parts;
		for(int j = 0; j < msg.parts.GetCount(); j++) {
			const CanonicalPart& p = msg.parts[j];
			if(p.type == "tool_result") {
				bool matched = false;
				if(!p.call_id.IsEmpty() && result_text_by_call.Find(p.call_id) >= 0) {
					for(int k = i; k >= 0 && !matched; k--)
						for(int w = 0; w < s.messages[k].parts.GetCount(); w++)
							if(s.messages[k].parts[w].type == "tool_use"
							   && s.messages[k].parts[w].call_id == p.call_id)
								matched = true;
				}
				if(matched)
					continue;
			}
			parts.Add(KiloPartJson(p, msg, s, sid, mid, result_text_by_call));
		}
		entry.Add("parts", Value(parts));
		messages.Add(Value(entry));
		prev_mid = mid;
	}
	ValueMap root;
	root.Add("info", Value(info));
	root.Add("messages", Value(messages));
	return Value(root);
}

static String ShellQuote(const String& path)
{
	String out = "'";
	for(int i = 0; i < path.GetCount(); i++) {
		if(path[i] == '\'')
			out << "'\\''";
		else
			out.Cat(path[i]);
	}
	out << "'";
	return out;
}

bool KiloImportFile(const String& file, const String& cwd, String& message)
{
	int exitcode = -1;
	String full = "cd " + ShellQuote(cwd.IsEmpty() ? String(".") : cwd) + " && kilo import "
	    + ShellQuote(file) + " 2>&1";
	FILE *f = ::popen(~full, "r");
	if(!f) {
		message = "cannot run kilo import";
		return false;
	}
	String out;
	char buf[4096];
	size_t n;
	while((n = ::fread(buf, 1, sizeof(buf), f)) > 0)
		out.Cat(buf, (int)n);
	int rc = ::pclose(f);
	if(rc < 0) {
		message = out.IsEmpty() ? String("kilo import failed to start") : out;
		return false;
	}
	if(WIFEXITED(rc)) {
		exitcode = WEXITSTATUS(rc);
		message = out;
		return exitcode == 0;
	}
	message = out.IsEmpty() ? String("kilo import was terminated abnormally") : out;
	return false;
}

static void ClaudeBlockJson(ValueMap& b, const CanonicalPart& p)
{
	if(p.type == "text" || p.type == "step") {
		b.Add("type", Value("text"));
		b.Add("text", Value(p.text));
	}
	else if(p.type == "reasoning") {
		b.Add("type", Value("thinking"));
		b.Add("thinking", Value(p.text));
	}
	else if(p.type == "tool_use") {
		b.Add("type", Value("tool_use"));
		b.Add("id", Value(p.call_id.IsEmpty() ? GenCallId() : p.call_id));
		b.Add("name", Value(p.tool.IsEmpty() ? String("tool") : p.tool));
		Value iv = ParseJsonValue(p.input_json);
		b.Add("input", iv.IsNull() ? Value(ValueMap()) : iv);
	}
	else if(p.type == "tool_result") {
		b.Add("type", Value("tool_result"));
		b.Add("tool_use_id", Value(p.call_id.IsEmpty() ? GenCallId() : p.call_id));
		Value ov = PartOutputText(p);
		b.Add("content", IsNull(ov) ? Value(String()) : ov);
	}
	else {
		b.Add("type", Value(p.type.IsEmpty() ? String("text") : p.type));
		b.Add("text", Value(p.text));
	}
}

bool WriteClaudeJsonl(const CanonicalSession& s, const String& path, String& error,
	const String& to_directory, const String& session_id_override)
{
	FileOut out(path);
	if(!out) {
		error = "cannot write " + path;
		return false;
	}
	bool kilo_source = s.source == "kilo";
	String sid = session_id_override.IsEmpty() ? s.id : session_id_override;
	String cwd = to_directory.IsEmpty() ? s.directory : to_directory;
	bool rehome = !to_directory.IsEmpty() || !session_id_override.IsEmpty();
	for(int i = 0; i < s.messages.GetCount(); i++) {
		const CanonicalMessage& msg = s.messages[i];
		if(s.source == "claude" && !msg.src_json.IsEmpty()) {
			if(rehome) {
				Value v = ParseJsonValue(msg.src_json);
				if(IsValueMap(v)) {
					ValueMap m = v;
					ValueMap rewritten;
					for(int k = 0; k < m.GetCount(); k++) {
						Value key = m.GetKey(k);
						String ks = IsString(key) ? String(key) : key.ToString();
						if(ks == "sessionId")
							rewritten.Add(key, Value(sid));
						else if(ks == "cwd")
							rewritten.Add(key, Value(cwd));
						else
							rewritten.Add(key, m.GetValue(k));
					}
					out.Put(DumpJson(Value(rewritten), false, false, true));
					out.Put("\n");
					continue;
				}
			}
			out.Put(msg.src_json);
			out.Put("\n");
			continue;
		}
		String iso = IsoTimestampMs(msg.stamp_ms);
		if(iso.IsEmpty())
			iso = IsoTimestampMs(NowMs());
		String uuid = msg.id.IsEmpty() ? GenClaudeUuid() : msg.id;
		ValueMap record;
		record.Add("type", Value(msg.role == "assistant" ? String("assistant") : String("user")));
		record.Add("timestamp", Value(iso));
		record.Add("sessionId", Value(sid));
		record.Add("cwd", Value(cwd));
		record.Add("uuid", Value(uuid));
		ValueMap message;
		message.Add("role", Value(msg.role == "assistant" ? String("assistant") : String("user")));
		if(msg.role == "assistant")
			message.Add("stop_reason", Value(String("end_turn")));
		ValueArray content;
		Vector<const CanonicalPart *> tool_parts;
		for(int j = 0; j < msg.parts.GetCount(); j++) {
			const CanonicalPart& p = msg.parts[j];
			if(p.type == "tool_result" && kilo_source)
				continue;
			ValueMap b;
			ClaudeBlockJson(b, p);
			content.Add(Value(b));
			if(p.type == "tool_use")
				tool_parts.Add(&p);
		}
		message.Add("content", Value(content));
		record.Add("message", Value(message));
		out.Put(DumpJson(Value(record), false, false, true));
		out.Put("\n");
		if(kilo_source) {
			for(int j = 0; j < tool_parts.GetCount(); j++) {
				const CanonicalPart& p = *tool_parts[j];
				if(p.output_json.IsEmpty())
					continue;
				String tool_uuid = GenClaudeUuid();
				ValueMap tresult;
				tresult.Add("type", Value(String("user")));
				tresult.Add("timestamp", Value(iso));
				tresult.Add("sessionId", Value(sid));
				tresult.Add("cwd", Value(cwd));
				tresult.Add("uuid", Value(tool_uuid));
				ValueMap tmsg;
				tmsg.Add("role", Value(String("user")));
				ValueArray tcontent;
				ValueMap tb;
				tb.Add("type", Value(String("tool_result")));
				tb.Add("tool_use_id", Value(p.call_id.IsEmpty() ? GenCallId() : p.call_id));
				Value ov = PartOutputText(p);
				tb.Add("content", IsNull(ov) ? Value(String()) : ov);
				tcontent.Add(Value(tb));
				tmsg.Add("content", Value(tcontent));
				tresult.Add("message", Value(tmsg));
				out.Put(DumpJson(Value(tresult), false, false, true));
				out.Put("\n");
			}
		}
	}
	if(out.IsError()) {
		error = "write error on " + path;
		return false;
	}
	out.Close();
	return true;
}

bool InstallClaudeJsonl(const String& directory, const String& session_id, const String& path,
	String& installed_path, String& error)
{
	if(directory.IsEmpty()) {
		error = "session has no directory; cannot locate ~/.claude/projects";
		return false;
	}
	String slug;
	for(int i = 0; i < directory.GetCount(); i++)
		slug.Cat(directory[i] == '/' ? '-' : directory[i]);
	String dir = AppendFileName(HomePath(".claude/projects"), slug);
	if(!DirectoryCreate(dir))
		if(!DirectoryExists(dir)) {
			error = "cannot create " + dir;
			return false;
		}
	String target = AppendFileName(dir, session_id + ".jsonl");
	if(FileExists(target)) {
		error = "claude session file already exists: " + target + " (remove it first)";
		return false;
	}
	String data = LoadFile(path);
	if(data.IsEmpty() && !FileExists(path)) {
		error = "cannot read " + path;
		return false;
	}
	FileOut out(target);
	if(!out) {
		error = "cannot write " + target;
		return false;
	}
	out.Put(data);
	out.Close();
	installed_path = target;
	return true;
}

END_UPP_NAMESPACE
