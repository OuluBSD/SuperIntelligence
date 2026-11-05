#include "AgentSessionManager.h"

NAMESPACE_UPP

static int64 DaysFromCivil(int y, int m, int d)
{
	y -= m <= 2;
	const int64 era = (y >= 0 ? y : y - 399) / 400;
	const unsigned yoe = static_cast<unsigned>(y - era * 400);
	const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + static_cast<int64>(doe) - 719468;
}

static void CivilFromDays(int64 z, int& y, int& m, int& d)
{
	z += 719468;
	const int64 era = (z >= 0 ? z : z - 146096) / 146097;
	const unsigned doe = static_cast<unsigned>(z - era * 146097);
	const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	const int64 yy = static_cast<int64>(yoe) + era * 400;
	const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	const unsigned mp = (5 * doy + 2) / 153;
	d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
	m = static_cast<int>(mp + (mp < 10 ? 3 : -9));
	y = static_cast<int>(yy + (m <= 2));
}

String IsoTimestampMs(int64 epoch_ms)
{
	if(epoch_ms <= 0)
		return String();
	int64 secs = epoch_ms / 1000;
	int64 days = secs / 86400;
	int64 rem = secs - days * 86400;
	int y, m, d;
	CivilFromDays(days, y, m, d);
	char buf[48];
	::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ", y, m, d,
		(int)(rem / 3600), (int)((rem % 3600) / 60), (int)(rem % 60));
	return String(buf);
}

static bool IsDigit(char c) { return c >= '0' && c <= '9'; }

static int ReadNum(const char *&p, int maxdigits)
{
	int value = 0, digits = 0;
	while(IsDigit(*p) && digits < maxdigits) {
		value = value * 10 + (*p - '0');
		p++;
		digits++;
	}
	return digits ? value : -1;
}

int64 IsoParseMs(const String& s)
{
	const char *p = ~s;
	int y = ReadNum(p, 4);
	if(y < 0 || *p != '-')
		return 0;
	p++;
	int mo = ReadNum(p, 2);
	if(mo < 1 || mo > 12 || *p != '-')
		return 0;
	p++;
	int d = ReadNum(p, 2);
	if(d < 1 || d > 31)
		return 0;
	int64 total = (DaysFromCivil(y, mo, d) * 86400) * 1000;
	if(!*p)
		return total;
	if(*p != 'T' && *p != ' ')
		return 0;
	p++;
	int h = ReadNum(p, 2);
	if(h < 0 || h > 23 || *p != ':')
		return 0;
	p++;
	int mi = ReadNum(p, 2);
	if(mi < 0 || mi > 59)
		return 0;
	int sec = 0;
	int64 frac_ms = 0;
	if(*p == ':') {
		p++;
		sec = ReadNum(p, 2);
		if(sec < 0 || sec > 60)
			return 0;
		if(*p == '.' || *p == ',') {
			p++;
			int scale = 100;
			while(IsDigit(*p)) {
				if(scale >= 1)
					frac_ms += (*p - '0') * scale;
				scale /= 10;
				p++;
			}
		}
	}
	total += (h * 3600 + mi * 60 + sec) * 1000 + frac_ms;
	if(*p == 'Z' || *p == 'z') {
		p++;
		return *p ? 0 : total;
	}
	if(*p == '+' || *p == '-') {
		bool plus = *p == '+';
		p++;
		int zh = ReadNum(p, 2);
		if(zh < 0)
			return 0;
		int zm = 0;
		if(*p == ':') {
			p++;
			zm = ReadNum(p, 2);
		} else if(IsDigit(*p))
			zm = ReadNum(p, 2);
		if(zm < 0 || zm > 59 || *p)
			return 0;
		int64 off = (zh * 3600 + zm * 60) * 1000;
		return plus ? total - off : total + off;
	}
	return *p ? 0 : total;
}

int64 TimestampValueToMs(const Value& v)
{
	if(v.IsNull())
		return 0;
	if(v.Is<bool>())
		return 0;
	if(v.Is<int>()) {
		int64 n = (int)v;
		return n > 10000000000LL ? n : n * 1000;
	}
	if(v.Is<int64>()) {
		int64 n = (int64)v;
		return n > 10000000000LL ? n : n * 1000;
	}
	if(IsNumber(v)) {
		double n = (double)v;
		return (int64)(n * (n > 10000000000.0 ? 1 : 1000));
	}
	if(IsString(v)) {
		String s = v;
		if(s.IsEmpty())
			return 0;
		return IsoParseMs(s);
	}
	return 0;
}

String TimestampText(const Value& v)
{
	int64 ms = TimestampValueToMs(v);
	return ms > 0 ? IsoTimestampMs(ms) : String();
}

String Capitalize(const String& s)
{
	if(s.IsEmpty())
		return s;
	return ToUpper(s.Left(1)) + ToLower(s.Mid(1));
}

String StripPrefix(const String& s, const String& prefix)
{
	return s.StartsWith(prefix) ? s.Mid(prefix.GetLength()) : s;
}

String HomePath(const char *rel)
{
	return AppendFileName(GetHomeDirectory(), rel);
}

String Sha256Hex(const String& data)
{
	return SHA256String(data);
}

int64 FileMtimeNs(const String& path)
{
	struct stat st;
	if(::stat(~path, &st) != 0)
		return -1;
	return (int64)st.st_mtim.tv_sec * 1000000000 + (int64)st.st_mtim.tv_nsec;
}

int64 FileSize64(const String& path)
{
	struct stat st;
	if(::stat(~path, &st) != 0)
		return -1;
	return (int64)st.st_size;
}

Value ParseJsonValue(const String& text)
{
	try {
		return ParseJSON(~text);
	}
	catch(...) {
		return Value();
	}
}

String ValueStr(const Value& v)
{
	return IsString(v) ? String(v) : (v.IsNull() ? String() : v.ToString());
}

bool Truthy(const Value& v)
{
	if(v.IsNull())
		return false;
	if(v.Is<bool>())
		return (bool)v;
	if(v.Is<int>())
		return (int)v != 0;
	if(v.Is<int64>())
		return (int64)v != 0;
	if(IsNumber(v))
		return (double)v != 0.0;
	if(IsString(v))
		return String(v).GetCount() > 0;
	if(IsValueArray(v)) {
		ValueArray a = v;
		return a.GetCount() > 0;
	}
	if(IsValueMap(v)) {
		ValueMap m = v;
		return m.GetCount() > 0;
	}
	return true;
}

String JsonEscape(const String& s, bool ensure_ascii)
{
	String out;
	out.Reserve(s.GetCount() + 8);
	out << '\"';
	if(ensure_ascii) {
		WString w = s.ToWString();
		for(int i = 0; i < w.GetCount(); i++) {
			int c = w[i];
			switch(c) {
			case '\"': out << "\\\""; break;
			case '\\': out << "\\\\"; break;
			case '\b': out << "\\b"; break;
			case '\f': out << "\\f"; break;
			case '\n': out << "\\n"; break;
			case '\r': out << "\\r"; break;
			case '\t': out << "\\t"; break;
			default:
				if(c < 0x20 || c > 0x7e) {
					if(c > 0xFFFF) {
						int cc = c - 0x10000;
						out << NFormat("\\u%04X\\u%04X", 0xD800 + (cc >> 10), 0xDC00 + (cc & 0x3FF));
					} else
						out << NFormat("\\u%04X", c);
				} else
					out.Cat(c);
				break;
			}
		}
	} else {
		const char *p = ~s;
		const char *e = p + s.GetCount();
		while(p < e) {
			char c = *p;
			switch(c) {
			case '\"': out << "\\\""; p++; break;
			case '\\': out << "\\\\"; p++; break;
			case '\b': out << "\\b"; p++; break;
			case '\f': out << "\\f"; p++; break;
			case '\n': out << "\\n"; p++; break;
			case '\r': out << "\\r"; p++; break;
			case '\t': out << "\\t"; p++; break;
			default:
				if((unsigned char)c < 0x20) {
					out << NFormat("\\u%04X", (unsigned char)c);
					p++;
				} else {
					out.Cat(c);
					p++;
				}
				break;
			}
		}
	}
	out << '\"';
	return out;
}

static String DoubleToJson(double n)
{
	if(n == (double)(int64)n && fabs(n) < 9.0e15)
		return Format("%lld", (int64)n);
	String a = Format("%.15g", n);
	double back = 0;
	bool ok = false;
	try {
		CParser p(~a);
		back = p.ReadDouble();
		ok = p.IsEof();
	}
	catch(...) {}
	if(!ok || back != n)
		a = Format("%.17g", n);
	if(a.Find(".") < 0 && a.Find("e") < 0 && a.Find("E") < 0)
		a << ".0";
	return a;
}

static void DumpJsonRec(String& out, const Value& v, bool pretty, bool ensure_ascii, int depth, bool nospace)
{
	if(v.GetType() == VOID_V) {
		out << "null";
		return;
	}
	if(v.GetType() == VALUEARRAY_V) {
		ValueArray a = v;
		if(a.GetCount() == 0) {
			out << "[]";
			return;
		}
		out << '[';
		if(pretty)
			out << '\n';
		for(int i = 0; i < a.GetCount(); i++) {
			if(i)
				out << (pretty ? ",\n" : (nospace ? "," : ", "));
			if(pretty)
				out << String(' ', (depth + 1) * 2);
			DumpJsonRec(out, a[i], pretty, ensure_ascii, depth + 1, nospace);
		}
		if(pretty)
			out << '\n' << String(' ', depth * 2);
		out << ']';
		return;
	}
	if(v.GetType() == VALUEMAP_V) {
		ValueMap m = v;
		if(m.GetCount() == 0) {
			out << "{}";
			return;
		}
		out << '{';
		if(pretty)
			out << '\n';
		for(int i = 0; i < m.GetCount(); i++) {
			if(i)
				out << (pretty ? ",\n" : (nospace ? "," : ", "));
			if(pretty)
				out << String(' ', (depth + 1) * 2);
			Value k = m.GetKey(i);
			out << JsonEscape(IsString(k) ? String(k) : k.ToString(), ensure_ascii);
			out << (nospace ? ":" : ": ");
			DumpJsonRec(out, m.GetValue(i), pretty, ensure_ascii, depth + 1, nospace);
		}
		if(pretty)
			out << '\n' << String(' ', depth * 2);
		out << '}';
		return;
	}
	if(IsString(v)) {
		out << JsonEscape(String(v), ensure_ascii);
		return;
	}
	if(v.IsNull()) {
		out << "null";
		return;
	}
	if(v.Is<bool>()) {
		out << ((bool)v ? "true" : "false");
		return;
	}
	if(v.Is<int>()) {
		out << Format("%d", (int)v);
		return;
	}
	if(v.Is<int64>()) {
		out << Format("%lld", (int64)v);
		return;
	}
	if(IsNumber(v)) {
		out << DoubleToJson((double)v);
		return;
	}
	if(IsString(v)) {
		out << JsonEscape(String(v), ensure_ascii);
		return;
	}
	if(v.GetType() == VALUEMAP_V) {
		ValueMap m = v;
		if(m.GetCount() == 0) {
			out << "{}";
			return;
		}
		out << '{';
		if(pretty)
			out << '\n';
		for(int i = 0; i < m.GetCount(); i++) {
			if(i)
				out << (pretty ? ",\n" : (nospace ? "," : ", "));
			if(pretty)
				out << String(' ', (depth + 1) * 2);
			Value k = m.GetKey(i);
			out << JsonEscape(IsString(k) ? String(k) : k.ToString(), ensure_ascii);
			out << (nospace ? ":" : ": ");
			DumpJsonRec(out, m.GetValue(i), pretty, ensure_ascii, depth + 1, nospace);
		}
		if(pretty)
			out << '\n' << String(' ', depth * 2);
		out << '}';
		return;
	}
	if(v.GetType() == VALUEARRAY_V) {
		ValueArray a = v;
		if(a.GetCount() == 0) {
			out << "[]";
			return;
		}
		out << '[';
		if(pretty)
			out << '\n';
		for(int i = 0; i < a.GetCount(); i++) {
			if(i)
				out << (pretty ? ",\n" : (nospace ? "," : ", "));
			if(pretty)
				out << String(' ', (depth + 1) * 2);
			DumpJsonRec(out, a[i], pretty, ensure_ascii, depth + 1, nospace);
		}
		if(pretty)
			out << '\n' << String(' ', depth * 2);
		out << ']';
		return;
	}
	out << JsonEscape(v.ToString(), ensure_ascii);
}

String DumpJson(const Value& v, bool pretty, bool ensure_ascii, bool nospace)
{
	String out;
	DumpJsonRec(out, v, pretty, ensure_ascii, 0, nospace);
	return out;
}

String ContentText(const Value& content)
{
	if(content.IsNull())
		return String();
	if(IsString(content))
		return String(content);
	if(!IsValueArray(content))
		return String();
	ValueArray a = content;
	Vector<String> chunks;
	for(int i = 0; i < a.GetCount(); i++) {
		const Value& item = a[i];
		if(!IsValueMap(item))
			continue;
		ValueMap m = item;
		String type = ValueStr(m.Get("type"));
		if(type == "text")
			chunks.Add(ValueStr(m.Get("text")));
		else if(type == "tool_use") {
			String name = ValueStr(m.Get("name"));
			if(name.IsEmpty())
				name = "tool";
			Value input = m.Get("input");
			if(input.IsNull())
				input = ValueMap();
			chunks.Add("Tool `" + name + "`\n\n```json\n"
				+ DumpJson(input, true, false) + "\n```");
		} else if(type == "tool_result") {
			Value cv = m.Get("content");
			if(IsString(cv))
				chunks.Add(String(cv));
			else if(!cv.IsNull())
				chunks.Add(DumpJson(cv, false, false));
		}
	}
	String out;
	for(int i = 0; i < chunks.GetCount(); i++) {
		if(chunks[i].IsEmpty())
			continue;
		if(!out.IsEmpty())
			out << "\n\n";
		out << chunks[i];
	}
	return out;
}

bool ParseArgsInt(const String& s, int& out)
{
	const char *p = ~s;
	bool neg = false;
	if(*p == '-') {
		neg = true;
		p++;
	}
	if(!IsDigit(*p))
		return false;
	int64 v = 0;
	while(IsDigit(*p)) {
		v = v * 10 + (*p - '0');
		if(v > 0x7fffffff)
			return false;
		p++;
	}
	if(*p)
		return false;
	out = neg ? (int)-v : (int)v;
	return true;
}

int64 NowMs()
{
	return (int64)::time(NULL) * 1000;
}

Value CursorLatestFromFile(const String& path)
{
	Value latest = Value(String());
	FileIn in(path);
	if(!in)
		return latest;
	String tail = LoadStream(in);
	Vector<String> lines = Split(tail, '\n', false);
	for(int i = lines.GetCount() - 1; i >= 0; i--) {
		Value v = ParseJsonValue(lines[i]);
		if(!IsValueMap(v))
			continue;
		ValueMap m = v;
		Value uuid = m.Get("uuid");
		Value ts = m.Get("timestamp");
		if((IsString(uuid) && !String(uuid).IsEmpty()) || (IsString(ts) && !String(ts).IsEmpty())) {
			ValueArray out;
			out.Add(uuid);
			out.Add(ts);
			latest = Value(out);
			break;
		}
	}
	return latest;
}

END_UPP_NAMESPACE
