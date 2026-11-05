#include "AgentSessionManager.h"

NAMESPACE_UPP

static uint64 GenSeed()
{
	static uint64 seq = 0;
	seq++;
	uint64 seed = ((uint64)NowMs() << 24) ^ (seq * 0x9E3779B97F4A7C15ull);
	seed ^= seed >> 33;
	seed *= 0xff51afd7ed558ccdull;
	seed ^= seed >> 33;
	return seed;
}

static String GenBase62(int n)
{
	static const char *alpha = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
	String out;
	uint64 seed = GenSeed();
	out.Reserve(n);
	for(int i = 0; i < n; i++) {
		seed = seed * 6364136223846793005ull + 1442695040888963407ull;
		out.Cat(alpha[(seed >> 33) % 62]);
	}
	return out;
}

String GenHexId(int n)
{
	static const char *alpha = "0123456789abcdef";
	String out;
	uint64 seed = GenSeed();
	out.Reserve(n);
	for(int i = 0; i < n; i++) {
		seed = seed * 6364136223846793005ull + 1442695040888963407ull;
		out.Cat(alpha[(seed >> 33) % 16]);
	}
	return out;
}

static String Hex12(uint64 v)
{
	char buf[12];
	for(int i = 11; i >= 0; i--) {
		buf[i] = "0123456789abcdef"[v & 15];
		v >>= 4;
	}
	return String(buf, 12);
}

static uint64 GenCounter()
{
	static uint64 counter = 0;
	counter++;
	return counter;
}

static const char *B62 = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

static String Base62Fixed(uint64 v, int n)
{
	String out;
	out.Reserve(n);
	for(int i = n - 1; i >= 0; i--) {
		out.Cat(B62[v % 62]);
		v /= 62;
	}
	return out;
}

String GenKiloId(const char *prefix)
{
	uint64 counter = GenCounter();
	String tail = Base62Fixed(counter, 4) + GenBase62(10);
	return String(prefix) + "_" + Hex12((uint64)NowMs()) + tail;
}

String GenCallId()
{
	return "call_" + GenHexId(24);
}

String GenClaudeUuid()
{
	String h = GenHexId(32);
	return h.Mid(0, 8) + "-" + h.Mid(8, 4) + "-4" + h.Mid(13, 3) + "-a"
	     + h.Mid(17, 3) + "-" + h.Mid(20, 12);
}

static void PartToJson(ValueMap& m, const CanonicalPart& p)
{
	m.Add("id", p.id.IsEmpty() ? Value() : Value(p.id));
	m.Add("type", Value(p.type));
	if(p.type == "text" || p.type == "reasoning" || p.type == "tool_result" || p.type == "step")
		m.Add("text", Value(p.text));
	if(!p.tool.IsEmpty())
		m.Add("tool", Value(p.tool));
	if(!p.status.IsEmpty())
		m.Add("status", Value(p.status));
	if(!p.call_id.IsEmpty())
		m.Add("callID", Value(p.call_id));
	if(!p.input_json.IsEmpty()) {
		Value iv = ParseJsonValue(p.input_json);
		if(iv.IsNull())
			m.Add("input", Value(ValueMap()));
		else
			m.Add("input", iv);
	}
	if(!p.output_json.IsEmpty()) {
		Value ov = ParseJsonValue(p.output_json);
		if(ov.IsNull())
			m.Add("output", Value(p.output_json));
		else
			m.Add("output", ov);
	}
	if(p.stamp_ms > 0)
		m.Add("stamp_ms", Value(p.stamp_ms));
	if(p.meta.GetType() == VALUEMAP_V && ValueMap(p.meta).GetCount() > 0)
		m.Add("meta", p.meta);
	if(!p.src_json.IsEmpty())
		m.Add("src", Value(p.src_json));
	else
		m.Add("src", Value());
}

Value CanonicalToJson(const CanonicalSession& s)
{
	ValueMap root;
	root.Add("version", Value(s.version));
	root.Add("id", Value(s.id));
	root.Add("source", Value(s.source));
	root.Add("title", Value(s.title));
	root.Add("directory", Value(s.directory));
	root.Add("created_ms", Value(s.created_ms));
	root.Add("updated_ms", Value(s.updated_ms));
	if(s.meta.IsNull())
		root.Add("meta", Value(ValueMap()));
	else
		root.Add("meta", s.meta);
	ValueArray msgs;
	for(int i = 0; i < s.messages.GetCount(); i++) {
		const CanonicalMessage& msg = s.messages[i];
		ValueMap mv;
		mv.Add("id", msg.id.IsEmpty() ? Value() : Value(msg.id));
		mv.Add("role", Value(msg.role));
		mv.Add("stamp_ms", Value(msg.stamp_ms));
		if(!msg.src_json.IsEmpty())
			mv.Add("src_json", Value(msg.src_json));
		else
			mv.Add("src_json", Value());
		ValueArray parts;
		for(int j = 0; j < msg.parts.GetCount(); j++) {
			ValueMap pm;
			PartToJson(pm, msg.parts[j]);
			parts.Add(Value(pm));
		}
		mv.Add("parts", Value(parts));
		msgs.Add(Value(mv));
	}
	root.Add("messages", Value(msgs));
	return Value(root);
}

static String GetStrField(ValueMap m, const char *name)
{
	return ValueStr(m.Get(name));
}

static int64 GetIntField(ValueMap m, const char *name)
{
	Value v = m.Get(name);
	if(IsNumber(v))
		return (int64)(double)v;
	return 0;
}

bool CanonicalFromJson(const Value& v, CanonicalSession& s, String& error)
{
	s = CanonicalSession();
	if(!IsValueMap(v)) {
		error = "canonical data is not a JSON object";
		return false;
	}
	ValueMap m = v;
	int version = (int)GetIntField(m, "version");
	if(version != 1) {
		error = NFormat("unsupported canonical version %d (expected 1)", version);
		return false;
	}
	s.id = GetStrField(m, "id");
	s.source = GetStrField(m, "source");
	s.title = GetStrField(m, "title");
	s.directory = GetStrField(m, "directory");
	s.created_ms = GetIntField(m, "created_ms");
	s.updated_ms = GetIntField(m, "updated_ms");
	Value meta = m.Get("meta");
	if(IsValueMap(meta))
		s.meta = meta;
	Value msgs = m.Get("messages");
	if(!IsValueArray(msgs)) {
		error = "canonical data has no messages array";
		return false;
	}
	ValueArray ma = msgs;
	for(int i = 0; i < ma.GetCount(); i++) {
		if(!IsValueMap(ma[i])) {
			error = NFormat("message %d is not an object", i);
			return false;
		}
		ValueMap mm = ma[i];
		CanonicalMessage& msg = s.messages.Add();
		msg.id = GetStrField(mm, "id");
		msg.role = GetStrField(mm, "role");
		msg.stamp_ms = GetIntField(mm, "stamp_ms");
		msg.src_json = GetStrField(mm, "src_json");
		Value parts = mm.Get("parts");
		if(IsValueArray(parts)) {
			ValueArray pa = parts;
			for(int j = 0; j < pa.GetCount(); j++) {
				if(!IsValueMap(pa[j])) {
					error = NFormat("message %d part %d is not an object", i, j);
					return false;
				}
				ValueMap pm = pa[j];
				CanonicalPart& part = msg.parts.Add();
				part.type = GetStrField(pm, "type");
				part.id = GetStrField(pm, "id");
				part.text = GetStrField(pm, "text");
				part.tool = GetStrField(pm, "tool");
				part.status = GetStrField(pm, "status");
				part.call_id = GetStrField(pm, "callID");
				Value iv = pm.Get("input");
				if(!iv.IsNull())
					part.input_json = DumpJson(iv, false, false);
				Value ov = pm.Get("output");
				if(IsString(ov))
					part.output_json = String(ov);
				else if(!ov.IsNull())
					part.output_json = DumpJson(ov, false, false);
				part.stamp_ms = GetIntField(pm, "stamp_ms");
				Value pmeta = pm.Get("meta");
				if(IsValueMap(pmeta))
					part.meta = pmeta;
				part.src_json = GetStrField(pm, "src");
			}
		}
	}
	return true;
}

END_UPP_NAMESPACE
