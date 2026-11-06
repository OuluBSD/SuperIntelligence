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

static bool IsClaudeUuid(const String& s)
{
	if(s.GetCount() != 36)
		return false;
	for(int i = 0; i < 36; i++) {
		if(i == 8 || i == 13 || i == 18 || i == 23) {
			if(s[i] != '-')
				return false;
		} else if(!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f')
		          || (s[i] >= 'A' && s[i] <= 'F')))
			return false;
	}
	return true;
}

static bool IsQuotaError(const CanonicalMessage& msg)
{
	for(int i = 0; i < msg.parts.GetCount(); i++) {
		Value marked = ValueMap(msg.parts[i].meta).Get("quota_error");
		if(marked.Is<bool>() && (bool)marked)
			return true;
	}
	return false;
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

static Value KiloAntigravityToolPart(const CanonicalPart& p, const CanonicalMessage& msg,
	const CanonicalSession& s, const String& sid, const String& mid, int64 generated_stamp)
{
	int64 stamp = generated_stamp > 0 ? generated_stamp
		: (p.stamp_ms > 0 ? p.stamp_ms : (msg.stamp_ms > 0 ? msg.stamp_ms : s.updated_ms));
	Value input_value = ParseJsonValue(p.input_json);
	ValueMap input;
	if(input_value.GetType() == VALUEMAP_V)
		input = ValueMap(input_value);
	ValueMap state;
	state.Add("status", Value(String("completed")));
	state.Add("input", Value(input));
	state.Add("output", PartOutputText(p));
	state.Add("title", Value(p.tool));
	state.Add("metadata", Value(ValueMap()));
	ValueMap time;
	time.Add("start", Value(stamp));
	time.Add("end", Value(stamp));
	state.Add("time", Value(time));
	ValueMap part;
	part.Add("id", Value(GenKiloId("prt")));
	part.Add("sessionID", Value(sid));
	part.Add("messageID", Value(mid));
	part.Add("type", Value(String("tool")));
	part.Add("callID", Value(p.call_id));
	part.Add("tool", Value(p.tool));
	part.Add("state", Value(state));
	ValueMap source_meta;
	if(p.meta.GetType() == VALUEMAP_V) {
		ValueMap meta = p.meta;
		for(int i = 0; i < meta.GetCount(); i++)
			source_meta.Add(meta.GetKey(i), meta.GetValue(i));
	}
	if(!p.payload_base64.IsEmpty())
		source_meta.Add("payload_base64", Value(p.payload_base64));
	if(p.extracted_strings.GetType() == VALUEARRAY_V)
		source_meta.Add("extracted_strings", p.extracted_strings);
	ValueMap metadata;
	metadata.Add("antigravity", Value(source_meta));
	part.Add("metadata", Value(metadata));
	return Value(part);
}

static Value KiloAntigravityTextPart(const CanonicalPart& srcpart, const String& sid,
	const String& mid, int64 generated_stamp)
{
	ValueMap text;
	text.Add("id", Value(GenKiloId("prt")));
	text.Add("sessionID", Value(sid));
	text.Add("messageID", Value(mid));
	text.Add("type", Value("text"));
	text.Add("text", Value(srcpart.text));
	ValueMap metadata;
	metadata.Add("idx", Value(ValueMap(srcpart.meta).Get("idx")));
	metadata.Add("step_type", Value(ValueMap(srcpart.meta).Get("step_type")));
	metadata.Add("status", Value(ValueMap(srcpart.meta).Get("status")));
	metadata.Add("source_stamp_ms", Value(srcpart.stamp_ms));
	if(!srcpart.payload_base64.IsEmpty())
		metadata.Add("payload_base64", Value(srcpart.payload_base64));
	if(srcpart.extracted_strings.GetType() == VALUEARRAY_V)
		metadata.Add("extracted_strings", srcpart.extracted_strings);
	text.Add("metadata", Value(metadata));
	if(generated_stamp > 0) {
		ValueMap time;
		time.Add("start", Value(generated_stamp));
		time.Add("end", Value(generated_stamp));
		text.Add("time", Value(time));
	}
	return Value(text);
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

struct KiloTargetProfile {
	String agent;
	String model_id;
	String provider_id;
	String variant;
	bool resolved = false;
};

static KiloTargetProfile ResolveKiloProfile(const CanonicalSession& s, const String& model_override)
{
	KiloTargetProfile profile;
	profile.agent = ValueStr(ValueMap(s.meta).Get("agent"));
	if(!model_override.IsEmpty()) {
		int slash = model_override.Find('/');
		if(slash <= 0 || slash >= model_override.GetCount() - 1)
			throw SessionError("--model must be PROVIDER/MODEL");
		profile.provider_id = model_override.Left(slash);
		profile.model_id = model_override.Mid(slash + 1);
		profile.resolved = true;
		if(profile.agent.IsEmpty())
			profile.agent = "code";
		return profile;
	}
	Value source_model = ValueMap(s.meta).Get("model");
	if(IsValueMap(source_model)) {
		ValueMap m = source_model;
		profile.model_id = ValueStr(m.Get("id"));
		profile.provider_id = ValueStr(m.Get("providerID"));
		profile.variant = ValueStr(m.Get("variant"));
		profile.resolved = !profile.model_id.IsEmpty() && !profile.provider_id.IsEmpty();
	}
	if(profile.resolved) {
		if(profile.agent.IsEmpty())
			profile.agent = "code";
		return profile;
	}

	String db = AppendFileName(HomePath(".local/share/kilo"), "kilo.db");
	if(!FileExists(db))
		goto defaults;
	{
		SqliteRo ro;
		if(!ro.Open(db))
			goto defaults;
		SqliteStatement q;
		if(!q.Prepare(ro, "SELECT agent,model FROM session WHERE directory=? AND model IS NOT NULL ORDER BY time_updated DESC LIMIT 1"))
			goto defaults;
		q.BindText(1, s.directory);
		if(q.Step() != SQLITE_STEP_ROW) {
			SqliteStatement fallback;
			if(!fallback.Prepare(ro, "SELECT agent,model FROM session WHERE model IS NOT NULL ORDER BY time_updated DESC LIMIT 1"))
				goto defaults;
			if(fallback.Step() != SQLITE_STEP_ROW)
				goto defaults;
			profile.agent = fallback.Text(0);
			Value model = ParseJsonValue(fallback.Text(1));
			if(IsValueMap(model)) {
				ValueMap m = model;
				profile.model_id = ValueStr(m.Get("id"));
				profile.provider_id = ValueStr(m.Get("providerID"));
				profile.variant = ValueStr(m.Get("variant"));
			}
		} else {
			profile.agent = q.Text(0);
			Value model = ParseJsonValue(q.Text(1));
			if(IsValueMap(model)) {
				ValueMap m = model;
				profile.model_id = ValueStr(m.Get("id"));
				profile.provider_id = ValueStr(m.Get("providerID"));
				profile.variant = ValueStr(m.Get("variant"));
			}
		}
		profile.resolved = !profile.model_id.IsEmpty() && !profile.provider_id.IsEmpty();
	}
defaults:
	if(profile.agent.IsEmpty())
		profile.agent = "code";
	if(profile.model_id.IsEmpty())
		profile.model_id = "unknown";
	if(profile.provider_id.IsEmpty())
		profile.provider_id = "unknown";
	return profile;
}

static Value KiloMessageInfo(const CanonicalMessage& msg, const CanonicalSession& s, const String& sid,
	const String& mid, const String& parent_mid, const KiloTargetProfile& profile,
	int64 assistant_completed_ms = 0, int64 message_time_override = 0)
{
	Value src = ParseJsonValue(msg.src_json);
	if(s.source == "kilo" && IsValueMap(src)) {
		ValueMap out;
		out.Add("id", Value(mid));
		out.Add("sessionID", Value(sid));
		ValueMap rest = CopyMapSkipping(ValueMap(src), "id", "sessionID",
			ValueStr(ValueMap(src).Get("role")) == "assistant" ? "parentID" : NULL);
		for(int i = 0; i < rest.GetCount(); i++)
			out.Add(rest.GetKey(i), rest.GetValue(i));
		if(ValueStr(rest.Get("role")) == "assistant")
			out.Add("parentID", Value(parent_mid.IsEmpty() ? mid : parent_mid));
		int64 stamp = msg.stamp_ms > 0 ? msg.stamp_ms : s.updated_ms;
		if(ValueStr(rest.Get("role")) == "assistant") {
			if(rest.Get("parentID").IsNull())
				out.Add("parentID", Value(parent_mid.IsEmpty() ? mid : parent_mid));
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
	String agent = profile.agent;
	ValueMap model;
	model.Add("providerID", Value(profile.provider_id));
	model.Add("modelID", Value(profile.model_id));
	if(!profile.variant.IsEmpty())
		model.Add("variant", Value(profile.variant));
	ValueMap m;
	m.Add("id", Value(mid));
	m.Add("sessionID", Value(sid));
	m.Add("role", Value(role));
	ValueMap tm;
	int64 message_time = message_time_override > 0 ? message_time_override
		: (msg.stamp_ms > 0 ? msg.stamp_ms : s.updated_ms);
	tm.Add("created", Value(message_time));
	if(role == "assistant")
		tm.Add("completed", Value(assistant_completed_ms > 0 ? assistant_completed_ms
			: message_time));
	m.Add("time", Value(tm));
	if(role == "assistant") {
		m.Add("parentID", Value(parent_mid.IsEmpty() ? mid : parent_mid));
		m.Add("modelID", Value(profile.model_id));
		m.Add("providerID", Value(profile.provider_id));
		m.Add("mode", Value(String("build")));
		m.Add("finish", Value(String("stop")));
		if(!profile.variant.IsEmpty())
			m.Add("variant", Value(profile.variant));
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

Value ToKiloExportJson(const CanonicalSession& s, bool fresh_session_id,
	const String& model_override, String& error)
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
	KiloTargetProfile profile = ResolveKiloProfile(s, model_override);
	String agent = ValueStr(ValueMap(s.meta).Get("agent"));
	if(agent.IsEmpty())
		agent = profile.agent;
	if(!agent.IsEmpty())
		info.Add("agent", Value(agent));
	if(s.source == "kilo" && IsValueMap(ValueMap(s.meta).Get("model")))
		info.Add("model", ValueMap(s.meta).Get("model"));
	else {
		ValueMap target_model;
		target_model.Add("id", Value(profile.model_id));
		target_model.Add("providerID", Value(profile.provider_id));
		if(!profile.variant.IsEmpty())
			target_model.Add("variant", Value(profile.variant));
		info.Add("model", Value(target_model));
	}
	if(s.source != "kilo") {
		ValueMap metadata;
		metadata.Add("conversionSource", Value(s.source));
		metadata.Add("sourceSessionId", Value(s.id));
		metadata.Add("syntheticUsage", Value(true));
		metadata.Add("syntheticModel", Value(!profile.resolved));
		info.Add("metadata", Value(metadata));
	}
	ValueMap time;
	time.Add("created", Value(s.created_ms > 0 ? s.created_ms : s.updated_ms));
	time.Add("updated", Value(s.updated_ms > 0 ? s.updated_ms : s.created_ms));
	info.Add("time", Value(time));
	Value mcost = ValueMap(s.meta).Get("cost");
	if(IsNumber(mcost))
		info.Add("cost", mcost);
	else if(s.source != "kilo")
		info.Add("cost", Value((double)0.0));
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
	else if(s.source != "kilo") {
		ValueMap tokens;
		tokens.Add("input", Value((int64)0));
		tokens.Add("output", Value((int64)0));
		tokens.Add("reasoning", Value((int64)0));
		ValueMap cache;
		cache.Add("read", Value((int64)0));
		cache.Add("write", Value((int64)0));
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
	if(s.source == "antigravity") {
		String active_user;
		int64 last_message_time = 0;
		for(int i = 0; i < s.messages.GetCount(); i++) {
			const CanonicalMessage& source = s.messages[i];
			String mid = GenKiloId("msg");
			int64 source_stamp = source.stamp_ms > 0 ? source.stamp_ms : s.updated_ms;
			int64 message_stamp = source_stamp;
			if(message_stamp <= last_message_time)
				message_stamp = last_message_time + 1;
			if(message_stamp <= 0)
				message_stamp = last_message_time > 0 ? last_message_time + 1 : NowMs();
			ValueMap entry;
			String parent = source.role == "user" ? String() : active_user;
			entry.Add("info", KiloMessageInfo(source, s, sid, mid, parent, profile,
				message_stamp, message_stamp));
			ValueArray parts;
			for(int j = 0; j < source.parts.GetCount(); j++) {
				const CanonicalPart& srcpart = source.parts[j];
				if(srcpart.type == "tool_use")
					parts.Add(KiloAntigravityToolPart(srcpart, source, s, sid, mid, message_stamp));
				else
					parts.Add(KiloAntigravityTextPart(srcpart, sid, mid, message_stamp));
			}
			entry.Add("parts", Value(parts));
			messages.Add(Value(entry));
			if(source.role == "user")
				active_user = mid;
			prev_mid = mid;
			last_message_time = message_stamp;
		}
		ValueMap root;
		root.Add("info", Value(info));
		root.Add("messages", Value(messages));
		return Value(root);
	}

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
		entry.Add("info", KiloMessageInfo(msg, s, sid, mid, prev_mid, profile));
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
		if(msg.role == "user")
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

static String ClaudeToolId(const CanonicalPart& p);
static String LookupClaudeToolId(const VectorMap<String, String>& ids, const CanonicalPart& p);

static void ClaudeBlockJson(ValueMap& b, const CanonicalPart& p, const String& tool_id)
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
		b.Add("id", Value(tool_id.IsEmpty() ? ClaudeToolId(p) : tool_id));
		b.Add("name", Value(p.tool.IsEmpty() ? String("tool") : p.tool));
		Value iv = ParseJsonValue(p.input_json);
		b.Add("input", iv.IsNull() ? Value(ValueMap()) : iv);
	}
	else if(p.type == "tool_result") {
		b.Add("type", Value("tool_result"));
		b.Add("tool_use_id", Value(tool_id.IsEmpty() ? ClaudeToolId(p) : tool_id));
		Value ov = PartOutputText(p);
		b.Add("content", IsNull(ov) ? Value(String()) : ov);
	}
	else {
		b.Add("type", Value("text"));
		b.Add("text", Value(p.text.IsEmpty() ? p.type : p.text));
	}
}

static void ClaudeCommonRecord(ValueMap& record, const String& type, const String& session_id,
	const String& cwd, const String& uuid, const String& parent_uuid, const String& timestamp,
	const String& title, const String& version, const String& source, const String& source_session_id)
{
	record.Add("parentUuid", parent_uuid.IsEmpty() ? Value() : Value(parent_uuid));
	record.Add("isSidechain", Value(false));
	record.Add("type", Value(type));
	record.Add("uuid", Value(uuid));
	record.Add("timestamp", Value(timestamp));
	record.Add("userType", Value(String("external")));
	record.Add("entrypoint", Value(String("cli")));
	record.Add("cwd", Value(cwd));
	record.Add("sessionId", Value(session_id));
	record.Add("session_id", Value(session_id));
	record.Add("version", Value(version));
	record.Add("slug", Value(SlugifyTitle(title)));
	ValueMap conversion;
	conversion.Add("source", Value(source));
	conversion.Add("sourceSessionId", Value(source_session_id));
	conversion.Add("syntheticUsage", Value(true));
	record.Add("conversion", Value(conversion));
}

static Value ClaudeUsageZero()
{
	ValueMap usage;
	usage.Add("output_tokens_details", Value());
	usage.Add("input_tokens", Value((int64)0));
	usage.Add("output_tokens", Value((int64)0));
	usage.Add("cache_creation_input_tokens", Value((int64)0));
	usage.Add("cache_read_input_tokens", Value((int64)0));
	ValueMap server_tool_use;
	server_tool_use.Add("web_search_requests", Value((int64)0));
	server_tool_use.Add("web_fetch_requests", Value((int64)0));
	usage.Add("server_tool_use", Value(server_tool_use));
	usage.Add("service_tier", Value());
	ValueMap cache_creation;
	cache_creation.Add("ephemeral_1h_input_tokens", Value((int64)0));
	cache_creation.Add("ephemeral_5m_input_tokens", Value((int64)0));
	usage.Add("cache_creation", Value(cache_creation));
	usage.Add("inference_geo", Value());
	usage.Add("iterations", Value());
	usage.Add("speed", Value());
	return Value(usage);
}

static String ClaudeToolId(const CanonicalPart& p)
{
	if(p.call_id.StartsWith("toolu_"))
		return p.call_id;
	return "toolu_" + GenHexId(24);
}

static String LookupClaudeToolId(const VectorMap<String, String>& ids, const CanonicalPart& p)
{
	int q = ids.Find(p.call_id);
	if(q >= 0)
		return ids[q];
	return ClaudeToolId(p);
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
	if(s.source != "claude" && !IsClaudeUuid(sid))
		sid = GenClaudeUuid();
	String cwd = to_directory.IsEmpty() ? s.directory : to_directory;
	bool rehome = !to_directory.IsEmpty() || !session_id_override.IsEmpty();
	String previous_uuid;
	String version = ValueStr(ValueMap(s.meta).Get("claude_version"));
	if(version.IsEmpty())
		version = "converted";
	VectorMap<String, String> tool_ids;
	for(int i = 0; i < s.messages.GetCount(); i++)
		for(int j = 0; j < s.messages[i].parts.GetCount(); j++) {
			const CanonicalPart& p = s.messages[i].parts[j];
			if(p.type == "tool_use" && !p.call_id.IsEmpty() && tool_ids.Find(p.call_id) < 0)
				tool_ids.Add(p.call_id, ClaudeToolId(p));
		}
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
						if(ks == "sessionId" || ks == "session_id")
							rewritten.Add(key, Value(sid));
						else if(ks == "cwd")
							rewritten.Add(key, Value(cwd));
						else
							rewritten.Add(key, m.GetValue(k));
					}
					out.Put(DumpJson(Value(rewritten), false, false, true));
					out.Put("\n");
					previous_uuid = ValueStr(m.Get("uuid"));
					continue;
				}
			}
			out.Put(msg.src_json);
			out.Put("\n");
			Value native = ParseJsonValue(msg.src_json);
			if(IsValueMap(native))
				previous_uuid = ValueStr(ValueMap(native).Get("uuid"));
			continue;
		}
		String iso = IsoTimestampMs(msg.stamp_ms);
		if(iso.IsEmpty())
			iso = IsoTimestampMs(NowMs());
		String uuid = GenClaudeUuid();
		String role = msg.role == "assistant" ? "assistant" : "user";
		ValueMap record;
		ClaudeCommonRecord(record, role, sid, cwd, uuid, previous_uuid, iso, s.title,
			version, s.source, s.id);
		ValueArray content;
		Vector<const CanonicalPart *> tool_parts;
		for(int j = 0; j < msg.parts.GetCount(); j++) {
			const CanonicalPart& p = msg.parts[j];
			if(p.type == "tool_result" && (kilo_source || s.source == "antigravity"))
				continue;
			ValueMap b;
			ClaudeBlockJson(b, p, LookupClaudeToolId(tool_ids, p));
			content.Add(Value(b));
			if(p.type == "tool_use")
				tool_parts.Add(&p);
		}
		ValueMap message;
		if(role == "assistant") {
			message.Add("diagnostics", Value());
			message.Add("id", Value(GenClaudeUuid()));
			message.Add("container", Value());
			message.Add("model", Value(String("<synthetic>")));
			message.Add("role", Value(role));
			message.Add("stop_details", Value());
			String stop_reason = IsQuotaError(msg) ? String("stop_sequence")
				: (tool_parts.GetCount() ? String("tool_use") : String("end_turn"));
			message.Add("stop_reason", Value(stop_reason));
			if(tool_parts.GetCount())
				message.Add("stop_sequence", Value());
			else
				message.Add("stop_sequence", Value(String()));
			message.Add("type", Value(String("message")));
			message.Add("usage", ClaudeUsageZero());
			message.Add("content", Value(content));
			message.Add("context_management", Value());
		} else {
			message.Add("role", Value(role));
			if(content.GetCount() == 1 && msg.parts.GetCount() == 1
			   && msg.parts[0].type == "text")
				message.Add("content", Value(msg.parts[0].text));
			else
				message.Add("content", Value(content));
		}
		record.Add("message", Value(message));
		if(IsQuotaError(msg)) {
			record.Add("error", Value(String("rate_limit")));
			record.Add("isApiErrorMessage", Value(true));
			record.Add("apiErrorStatus", Value(429));
		}
		out.Put(DumpJson(Value(record), false, false, true));
		out.Put("\n");
		previous_uuid = uuid;
		if(kilo_source || s.source == "antigravity") {
			for(int j = 0; j < tool_parts.GetCount(); j++) {
				const CanonicalPart& p = *tool_parts[j];
				if(p.output_json.IsEmpty())
					continue;
				String tool_uuid = GenClaudeUuid();
				ValueMap tresult;
				ClaudeCommonRecord(tresult, "user", sid, cwd, tool_uuid, previous_uuid, iso, s.title,
					version, s.source, s.id);
				ValueMap tmsg;
				tmsg.Add("role", Value(String("user")));
				ValueArray tcontent;
				ValueMap tb;
				tb.Add("type", Value(String("tool_result")));
				tb.Add("tool_use_id", Value(LookupClaudeToolId(tool_ids, p)));
				tb.Add("content", PartOutputText(p));
				tcontent.Add(Value(tb));
				tmsg.Add("content", Value(tcontent));
				tresult.Add("message", Value(tmsg));
				out.Put(DumpJson(Value(tresult), false, false, true));
				out.Put("\n");
				previous_uuid = tool_uuid;
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
