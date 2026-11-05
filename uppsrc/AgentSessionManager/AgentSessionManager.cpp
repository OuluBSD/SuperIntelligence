#include "AgentSessionManager.h"

NAMESPACE_UPP

static const char *MANUAL =
R"SESS(AGENT-SESSION-MANAGER(1)         User Commands        AGENT-SESSION-MANAGER(1)

NAME
    AgentSessionManager - list, dump, convert, and monitor Claude, Kilo, and
    Antigravity sessions

SYNOPSIS
    AgentSessionManager [--source SOURCE] list [--limit N]
    AgentSessionManager [--source SOURCE] dump SESSION [--all | --range START END | --tail N]
            [--since ISO_DATETIME]
    AgentSessionManager [--source SOURCE] state SESSION [--field running|waiting]
    AgentSessionManager [--source SOURCE] changes SESSION [--cookie TOKEN]
    AgentSessionManager [--source SOURCE] canonical SESSION [--out FILE] [--force]
    AgentSessionManager [--source SOURCE] convert SESSION TARGET [--out FILE]
            [--emit-canonical FILE] [--from-canonical FILE] [--force] [--dry-run]
            [--import] [--install] [--keep-id]
    AgentSessionManager --man

SOURCES
    all          All supported tools (the default)
    claude       Claude Code JSONL sessions in ~/.claude/projects
    kilo         Kilo sessions in ~/.local/share/kilo/kilo.db
    antigravity  Antigravity CLI conversation databases in ~/.gemini

COMMANDS
    list         List sessions. Tab-separated columns are source, session ID,
                 running, waiting, and title. Use --limit to show the newest
                 N records. Cached results are refreshed when source files'
                 modification time or size changes; smaller files are also
                 content-hashed. The large Kilo database is stat-checked to
                 avoid hashing hundreds of megabytes on every invocation.

    dump         Export session entries as Markdown. By default all available
                 entries are printed. --range uses inclusive, 1-based entry
                 numbers; --tail selects the last N entries. --since filters
                 to entries newer than an ISO-8601 datetime (UTC is assumed if
                 no timezone is supplied). It can be combined with --tail or
                 --range. Each entry includes its timestamp when available.
                 Example datetime: 2026-11-02T13:00:00Z

    state        Print JSON booleans {"running": ..., "waiting": ...}.
                 --field running or --field waiting prints a single lowercase
                 boolean, useful in shell scripts. State is inferred from
                 each tool's persisted session data, not a guarantee that a
                 process is currently alive.

    changes      Compare a session with a previously returned cookie and
                 report whether its latest message/step changed. Without --cookie, this
                 establishes a baseline and reports has_new=false. Save the
                 returned cookie and pass it to the next check. This works
                 even when individual message timestamps are unavailable.

    canonical    Export a session as versioned canonical JSON (version 1):
                 a lossless, agent-agnostic intermediate representation with
                 message parts (text, tool_use, tool_result, reasoning, step)
                 and verbatim source payloads. --out FILE writes it (default:
                 stdout). This is the middle format used by convert.

    convert      Convert a session to another agent format. TARGET is kilo
                 or claude (antigravity is export-only). The conversion reads
                 the source read-only, builds the canonical representation,
                 and writes a target interchange file: for kilo a
                 `kilo import`-compatible JSON export, for claude a JSONL
                 transcript. --import additionally runs `kilo import` on the
                 written file (cwd is the session directory), creating the
                 session in the live Kilo database with a fresh session ID
                 unless --keep-id. --install places the converted JSONL into
                 ~/.claude/projects/<directory>/ for claude (claude target
                 only; refuses to overwrite an existing session file).
                 --emit-canonical saves the intermediate
                 canonical JSON. --from-canonical FILE converts a previously
                 emitted canonical file instead of reading a session.
                 --dry-run validates and reports counts without writing.
                 Output files are never overwritten without --force.

CANONICAL FORMAT
    Versioned JSON (version 1): id, source, title, directory, created_ms,
    updated_ms, meta, and messages. Each message has id, role, stamp_ms,
    src_json (verbatim source record when available), and typed parts: text,
    tool_use (tool, status, callID, input), tool_result, reasoning, step
    (antigravity; meta carries idx/step_type/status). Verbatim src payloads
    make kilo-to-kilo and claude-to-claude conversions byte-faithful.
    The Kilo export file produced for TARGET kilo matches `kilo export` and
    can be imported with `kilo import` (the official write path; the live
    kilo.db is never modified directly by this tool).

OPTIONS
    --source SOURCE  Limit a command to all, claude, kilo, or antigravity.
                     It may appear before the command or after it.
    -h, --help       Show concise command help. Use COMMAND --help for options.
    --man            Show this full manual.

EXAMPLES
    AgentSessionManager list
    AgentSessionManager list --source kilo --limit 20
    AgentSessionManager dump --source claude SESSION_ID --tail 10
    AgentSessionManager dump SESSION_ID --since 2026-11-02T13:00:00Z --tail 20
    AgentSessionManager dump --source antigravity SESSION_ID --range 5 12
    AgentSessionManager state --source kilo SESSION_ID --field running
    AgentSessionManager changes --source claude SESSION_ID
    AgentSessionManager changes --source claude SESSION_ID --cookie TOKEN_FROM_LAST_CHECK
    AgentSessionManager canonical --source antigravity SESSION_ID --out session.canonical.json
    AgentSessionManager convert --source antigravity SESSION_ID kilo
    AgentSessionManager convert --source antigravity SESSION_ID kilo --import
    AgentSessionManager convert --from-canonical session.canonical.json kilo --dry-run

SESSION IDS
    A full ID or an unambiguous prefix is accepted. If a prefix matches more
    than one session, use a longer prefix. The default source searches all
    providers and may report ambiguity when IDs overlap.

EXIT STATUS
    0 on success; nonzero for invalid input, missing/ambiguous session IDs,
    or source database errors.

FILES
    ~/.cache/session-tool/sessions.json   Rebuildable listing cache
    ~/.claude/projects/                   Claude session JSONL files
    ~/.local/share/kilo/kilo.db           Kilo session database
    ~/.gemini/antigravity-cli/conversation_summaries.db
                                          Antigravity conversation index (newer versions;
                                          older versions use cache/conversation_metadata.json)

SEE ALSO
    AgentSessionManager --help, AgentSessionManager list --help, AgentSessionManager dump --help,
    AgentSessionManager state --help, AgentSessionManager changes --help,
    kilo import --help, kilo export --help
)SESS";

static const char *HELP =
R"SESS(usage: AgentSessionManager [-h] [--source {all,kilo,claude,antigravity}]
                           {list,dump,state,changes,canonical,convert} ...

Inspect Claude Code, Kilo, and Antigravity sessions.

positional arguments:
  {list,dump,state,changes,canonical,convert}
    list                list sessions
    dump                dump session content as Markdown
    state               report running/waiting booleans
    changes             check for new session activity using a cookie
    canonical           export a session as canonical JSON
    convert             convert a session to another agent format

options:
  -h, --help            show this help message and exit
  --source {all,kilo,claude,antigravity}

Use --man for examples, cookie workflow, cache details, and all supported use cases.
)SESS";

struct Opts {
	String source;
	String command;
	String session_id;
	int    limit;
	bool   has_limit;
	bool   all_flag;
	int    range_start, range_end;
	bool   has_range;
	int    tail;
	bool   has_tail;
	String since;
	bool   has_since;
	String field;
	bool   has_field;
	String cookie;
	bool   has_cookie;
	String span_name;
	String target;
	String out;
	bool   has_out;
	String emit_canonical;
	String from_canonical;
	bool   force;
	bool   dry_run;
	bool   import_flag;
	bool   install_flag;
	bool   keep_id;

	Opts()
	{
		source = "all";
		limit = 0;
		has_limit = false;
		all_flag = false;
		range_start = 0;
		range_end = 0;
		has_range = false;
		tail = 0;
		has_tail = false;
		has_since = false;
		has_field = false;
		has_cookie = false;
		has_out = false;
		force = false;
		dry_run = false;
		import_flag = false;
		install_flag = false;
		keep_id = false;
	}
};

static const char *USAGE_TOP =
"usage: AgentSessionManager [-h] [--source {all,kilo,claude,antigravity}]\n"
"                           {list,dump,state,changes,canonical,convert} ...\n";

static const char *USAGE_CANONICAL =
"usage: AgentSessionManager canonical [-h] [--source {all,kilo,claude,antigravity}]\n"
"                                     [--out FILE] [--force]\n"
"                                     session_id\n";

static const char *USAGE_CONVERT =
"usage: AgentSessionManager convert [-h] [--source {all,kilo,claude,antigravity}]\n"
"                                   [--out FILE] [--emit-canonical FILE]\n"
"                                   [--from-canonical FILE] [--force] [--dry-run]\n"
"                                   [--import] [--install] [--keep-id]\n"
"                                   session_id {kilo,claude}\n";

static const char *USAGE_LIST =
"usage: AgentSessionManager list [-h] [--source {all,kilo,claude,antigravity}]\n"
"                                [--limit LIMIT]\n";

static const char *USAGE_DUMP =
"usage: AgentSessionManager dump [-h] [--source {all,kilo,claude,antigravity}]\n"
"                                [--all | --range START END | --tail COUNT]\n"
"                                [--since DATETIME]\n"
"                                session_id\n";

static const char *USAGE_STATE =
"usage: AgentSessionManager state [-h] [--source {all,kilo,claude,antigravity}]\n"
"                                 [--field {running,waiting}]\n"
"                                 session_id\n";

static const char *USAGE_CHANGES =
"usage: AgentSessionManager changes [-h]\n"
"                                   [--source {all,kilo,claude,antigravity}]\n"
"                                   [--cookie COOKIE]\n"
"                                   session_id\n";

struct ArgErr {
	const char *usage;
	String prefix;
	String message;
};

static bool ValidSource(const String& s)
{
	return s == "all" || s == "kilo" || s == "claude" || s == "antigravity";
}

static bool ValidCommand(const String& s)
{
	return s == "list" || s == "dump" || s == "state" || s == "changes"
	    || s == "canonical" || s == "convert";
}

static const char *SubUsage(const String& command)
{
	if(command == "list")
		return USAGE_LIST;
	if(command == "dump")
		return USAGE_DUMP;
	if(command == "state")
		return USAGE_STATE;
	if(command == "changes")
		return USAGE_CHANGES;
	if(command == "canonical")
		return USAGE_CANONICAL;
	if(command == "convert")
		return USAGE_CONVERT;
	return USAGE_TOP;
}

static String SubPrefix(const String& command)
{
	return "AgentSessionManager " + command;
}

static bool TakeValue(const Vector<String>& argv, int& i, const char *name, String& out, ArgErr& err)
{
	if(i + 1 >= argv.GetCount()) {
		err.message = String("argument ") + name + ": expected one argument";
		return false;
	}
	out = argv[++i];
	return true;
}

static bool TakeInt(const Vector<String>& argv, int& i, const char *name, int& out, ArgErr& err)
{
	String v;
	if(!TakeValue(argv, i, name, v, err))
		return false;
	if(!ParseArgsInt(v, out)) {
		err.message = String("argument ") + name + ": invalid int value: '" + v + "'";
		return false;
	}
	return true;
}

static bool ParseArgs(const Vector<String>& argv, Opts& o, ArgErr& err)
{
	err.usage = USAGE_TOP;
	err.prefix = "AgentSessionManager";
	for(int i = 0; i < argv.GetCount(); i++) {
		const String& a = argv[i];
		bool in_sub = !o.command.IsEmpty();
		if(in_sub) {
			err.usage = SubUsage(o.command);
			err.prefix = SubPrefix(o.command);
		}
		if(a == "--source") {
			String v;
			if(!TakeValue(argv, i, "--source", v, err))
				return false;
			if(!ValidSource(v)) {
				err.message = "argument --source: invalid choice: '" + v
					+ "' (choose from 'all', 'kilo', 'claude', 'antigravity')";
				return false;
			}
			o.source = v;
		}
		else if(a == "--limit" && o.command == "list") {
			if(!TakeInt(argv, i, "--limit", o.limit, err))
				return false;
			o.has_limit = true;
		}
		else if(a == "--all" && o.command == "dump") {
			if(!o.span_name.IsEmpty()) {
				err.message = "argument --all: not allowed with argument " + o.span_name;
				return false;
			}
			o.all_flag = true;
			o.span_name = "--all";
		}
		else if(a == "--range" && o.command == "dump") {
			if(!o.span_name.IsEmpty() && o.span_name != "--range") {
				err.message = "argument --range: not allowed with argument " + o.span_name;
				return false;
			}
			if(!TakeInt(argv, i, "--range", o.range_start, err))
				return false;
			if(!TakeInt(argv, i, "--range", o.range_end, err))
				return false;
			o.has_range = true;
			o.span_name = "--range";
		}
		else if(a == "--tail" && o.command == "dump") {
			if(!o.span_name.IsEmpty() && o.span_name != "--tail") {
				err.message = "argument --tail: not allowed with argument " + o.span_name;
				return false;
			}
			if(!TakeInt(argv, i, "--tail", o.tail, err))
				return false;
			o.has_tail = true;
			o.span_name = "--tail";
		}
		else if(a == "--since" && o.command == "dump") {
			if(!TakeValue(argv, i, "--since", o.since, err))
				return false;
			o.has_since = true;
		}
		else if(a == "--field" && o.command == "state") {
			String v;
			if(!TakeValue(argv, i, "--field", v, err))
				return false;
			if(v != "running" && v != "waiting") {
				err.message = "argument --field: invalid choice: '" + v
					+ "' (choose from 'running', 'waiting')";
				return false;
			}
			o.field = v;
			o.has_field = true;
		}
		else if(a == "--cookie" && o.command == "changes") {
			if(!TakeValue(argv, i, "--cookie", o.cookie, err))
				return false;
			o.has_cookie = true;
		}
		else if(a == "--out" && (o.command == "canonical" || o.command == "convert")) {
			if(!TakeValue(argv, i, "--out", o.out, err))
				return false;
			o.has_out = true;
		}
		else if(a == "--force" && (o.command == "canonical" || o.command == "convert"))
			o.force = true;
		else if(a == "--emit-canonical" && o.command == "convert") {
			if(!TakeValue(argv, i, "--emit-canonical", o.emit_canonical, err))
				return false;
		}
		else if(a == "--from-canonical" && o.command == "convert") {
			if(!TakeValue(argv, i, "--from-canonical", o.from_canonical, err))
				return false;
		}
		else if(a == "--dry-run" && o.command == "convert")
			o.dry_run = true;
		else if(a == "--import" && o.command == "convert")
			o.import_flag = true;
		else if(a == "--install" && o.command == "convert")
			o.install_flag = true;
		else if(a == "--keep-id" && o.command == "convert")
			o.keep_id = true;
		else if(a.StartsWith("-")) {
			err.message = "unrecognized arguments: " + a;
			return false;
		}
		else if(o.command.IsEmpty()) {
			if(!ValidCommand(a)) {
				err.message = "argument command: invalid choice: '" + a
					+ "' (choose from 'list', 'dump', 'state', 'changes', 'canonical', 'convert')";
				return false;
			}
			o.command = a;
		}
		else if(o.session_id.IsEmpty())
			o.session_id = a;
		else if(o.command == "convert" && o.target.IsEmpty()) {
			if(a != "kilo" && a != "claude") {
				err.message = "argument target: invalid choice: '" + a + "' (choose from 'kilo', 'claude')";
				return false;
			}
			o.target = a;
		}
		else {
			err.usage = USAGE_TOP;
			err.prefix = "AgentSessionManager";
			err.message = "unrecognized arguments: " + a;
			return false;
		}
	}
	return true;
}

static SessionState SourceState(const SessionEntry& s)
{
	if(s.source == "claude")
		return ClaudeState(s);
	if(s.source == "kilo")
		return KiloState(s);
	return AntigravityState(s);
}

static Vector<MessageEntry> SourceMessages(const SessionEntry& s)
{
	if(s.source == "claude")
		return ClaudeMessages(s);
	if(s.source == "kilo")
		return KiloMessages(s);
	return AntigravityMessages(s);
}

static String TabsToSpaces(const String& s)
{
	String out;
	out.Reserve(s.GetCount());
	for(int i = 0; i < s.GetCount(); i++)
		out.Cat(s[i] == '\t' ? ' ' : s[i]);
	return out;
}

END_UPP_NAMESPACE

using namespace Upp;

CONSOLE_APP_MAIN
{
	const Vector<String>& argv = CommandLine();
	for(int i = 0; i < argv.GetCount(); i++) {
		if(argv[i] == "--man") {
			Cout() << MANUAL;
			return;
		}
	}
	Opts o;
	ArgErr err;
	for(int i = 0; i < argv.GetCount(); i++) {
		if(argv[i] == "-h" || argv[i] == "--help") {
			Cout() << HELP;
			return;
		}
	}
	if(!ParseArgs(argv, o, err)) {
		Cerr() << err.usage << err.prefix << ": error: " << err.message << "\n";
		SetExitCode(2);
		return;
	}
	if(o.command.IsEmpty()) {
		Cerr() << USAGE_TOP << "AgentSessionManager: error: the following arguments are required: command\n";
		SetExitCode(2);
		return;
	}
	if(o.command == "canonical" && o.session_id.IsEmpty()) {
		Cerr() << USAGE_CANONICAL << "AgentSessionManager canonical: error: the following arguments are required: session_id\n";
		SetExitCode(2);
		return;
	}
	if(o.command == "convert") {
		if(o.target.IsEmpty() && !o.from_canonical.IsEmpty()
		   && (o.session_id == "kilo" || o.session_id == "claude")) {
			o.target = o.session_id;
			o.session_id.Clear();
		}
		if(o.target.IsEmpty()) {
			Cerr() << USAGE_CONVERT << "AgentSessionManager convert: error: the following arguments are required: target\n";
			SetExitCode(2);
			return;
		}
		if(o.session_id.IsEmpty() && o.from_canonical.IsEmpty()) {
			Cerr() << USAGE_CONVERT << "AgentSessionManager convert: error: the following arguments are required: session_id\n";
			SetExitCode(2);
			return;
		}
		if(o.import_flag && o.target != "kilo") {
			Cerr() << USAGE_CONVERT << "AgentSessionManager convert: error: argument --import: only valid with target kilo\n";
			SetExitCode(2);
			return;
		}
		if(o.install_flag && o.target != "claude") {
			Cerr() << USAGE_CONVERT << "AgentSessionManager convert: error: argument --install: only valid with target claude\n";
			SetExitCode(2);
			return;
		}
		if(o.import_flag && o.install_flag) {
			Cerr() << USAGE_CONVERT << "AgentSessionManager convert: error: argument --install: not allowed with argument --import\n";
			SetExitCode(2);
			return;
		}
	}
	if(o.command != "list" && o.command != "canonical" && o.command != "convert" && o.session_id.IsEmpty()) {
		Cerr() << SubUsage(o.command) << SubPrefix(o.command)
		       << ": error: the following arguments are required: session_id\n";
		SetExitCode(2);
		return;
	}
	try {
		if(o.command == "list") {
			Vector<SessionEntry> found = CachedSessions(o.source);
			if(o.has_limit) {
				if(o.limit < 0) {
					Cerr() << USAGE_TOP << "AgentSessionManager: error: limit cannot be negative\n";
					SetExitCode(2);
					return;
				}
				if(o.limit < found.GetCount())
					found.SetCount(o.limit);
			}
			for(int i = 0; i < found.GetCount(); i++) {
				SessionState st = SourceState(found[i]);
				Cout() << found[i].source << "\t" << found[i].id << "\t"
				       << (st.running ? "true" : "false") << "\t"
				       << (st.waiting ? "true" : "false") << "\t"
				       << TabsToSpaces(found[i].title) << "\n";
			}
		}
		else if(o.command == "dump") {
			SessionEntry s = FindSession(o.source, o.session_id);
			Vector<MessageEntry> entries = SourceMessages(s);
			if(o.has_since && !o.since.IsEmpty()) {
				int64 since = IsoParseMs(o.since);
				if(since <= 0)
					throw SessionError("invalid timestamp '" + o.since + "'; use ISO-8601");
				Vector<MessageEntry> filtered;
				for(int i = 0; i < entries.GetCount(); i++) {
					if(entries[i].stamp.IsEmpty())
						continue;
					int64 ms = IsoParseMs(entries[i].stamp);
					if(ms > since)
						filtered.Add(entries[i]);
				}
				entries = pick(filtered);
			}
			if(o.has_range) {
				if(o.range_start < 1 || o.range_end < o.range_start) {
					Cerr() << USAGE_TOP << "AgentSessionManager: error: range must satisfy 1 <= START <= END\n";
					SetExitCode(2);
					return;
				}
				Vector<MessageEntry> sliced;
				for(int i = o.range_start - 1; i < o.range_end && i < entries.GetCount(); i++)
					sliced.Add(entries[i]);
				entries = pick(sliced);
			}
			else if(o.has_tail) {
				if(o.tail < 0) {
					Cerr() << USAGE_TOP << "AgentSessionManager: error: tail count cannot be negative\n";
					SetExitCode(2);
					return;
				}
				Vector<MessageEntry> last;
				int start = entries.GetCount() > o.tail ? entries.GetCount() - o.tail : 0;
				for(int i = start; i < entries.GetCount(); i++)
					last.Add(entries[i]);
				entries = pick(last);
			}
			Cout() << "# " << s.title << "\n\nSource: " << s.source << " | Session: `"
			       << s.id << "` | Directory: `" << s.directory << "`\n\n";
			for(int i = 0; i < entries.GetCount(); i++) {
				Cout() << "## " << Capitalize(entries[i].role) << " " << (i + 1) << " - "
				       << (entries[i].stamp.IsEmpty() ? String("timestamp unavailable") : entries[i].stamp)
				       << "\n\n" << entries[i].text << "\n\n";
			}
		}
		else if(o.command == "state") {
			SessionEntry s = FindSession(o.source, o.session_id);
			SessionState st = SourceState(s);
			if(o.has_field) {
				bool v = o.field == "running" ? st.running : st.waiting;
				Cout() << (v ? "true" : "false") << "\n";
			} else {
				ValueMap out;
				out.Add("running", Value(st.running));
				out.Add("waiting", Value(st.waiting));
				Cout() << DumpJson(Value(out), false, true) << "\n";
			}
		}
		else if(o.command == "changes") {
			SessionEntry s = FindSession(o.source, o.session_id);
			bool has_new = CheckCookie(s, o.cookie);
			String cookie = EncodeCookie(s);
			ValueMap out;
			out.Add("has_new", Value(has_new));
			out.Add("cookie", Value(cookie));
			Cout() << DumpJson(Value(out), false, true) << "\n";
		}
		else if(o.command == "canonical") {
			SessionEntry s = FindSession(o.source, o.session_id);
			CanonicalSession cs = ToCanonical(s);
			String json = DumpJson(CanonicalToJson(cs), true, false) + "\n";
			int partcount = 0;
			for(int i = 0; i < cs.messages.GetCount(); i++)
				partcount += cs.messages[i].parts.GetCount();
			if(o.has_out && o.out != "-") {
				if(FileExists(o.out) && !o.force)
					throw SessionError("output file exists: " + o.out + " (use --force)");
				FileOut f(o.out);
				if(!f)
					throw SessionError("cannot write " + o.out);
				f.Put(json);
				f.Close();
				Cout() << "canonical written: " << o.out << " (" << cs.messages.GetCount()
				       << " messages, " << partcount << " parts)\n";
			} else
				Cout() << json;
		}
		else if(o.command == "convert") {
			CanonicalSession cs;
			if(!o.from_canonical.IsEmpty()) {
				Value v = ParseJsonValue(LoadFile(o.from_canonical));
				String cerr2;
				if(!CanonicalFromJson(v, cs, cerr2))
					throw SessionError(cerr2);
			} else {
				SessionEntry s = FindSession(o.source, o.session_id);
				cs = ToCanonical(s);
			}
			int partcount = 0;
			for(int i = 0; i < cs.messages.GetCount(); i++)
				partcount += cs.messages[i].parts.GetCount();
			if(!o.emit_canonical.IsEmpty()) {
				if(FileExists(o.emit_canonical) && !o.force)
					throw SessionError("output file exists: " + o.emit_canonical + " (use --force)");
				FileOut f(o.emit_canonical);
				if(!f)
					throw SessionError("cannot write " + o.emit_canonical);
				f.Put(DumpJson(CanonicalToJson(cs), true, false));
				f.Put("\n");
				f.Close();
			}
			String outpath = o.has_out ? o.out
				: String("converted-") + o.target + "-" + cs.id
				    + (o.target == "kilo" ? String(".json") : String(".jsonl"));
			if(o.dry_run) {
				Cout() << "dry-run: " << cs.source << " " << cs.id << " -> " << o.target << "\n";
				Cout() << "messages: " << cs.messages.GetCount() << ", parts: " << partcount << "\n";
				Cout() << "out: " << outpath << "\n";
			}
			else if(o.target == "kilo") {
				if(FileExists(outpath) && !o.force)
					throw SessionError("output file exists: " + outpath + " (use --force)");
				String kerr;
				Value ev = ToKiloExportJson(cs, !o.keep_id, kerr);
				if(!kerr.IsEmpty())
					throw SessionError(kerr);
				FileOut f(outpath);
				if(!f)
					throw SessionError("cannot write " + outpath);
				f.Put(DumpJson(ev, true, false));
				f.Put("\n");
				f.Close();
				Cout() << "kilo export written: " << outpath << " (" << cs.messages.GetCount()
				       << " messages, " << partcount << " parts)\n";
				if(o.import_flag) {
					String impmsg;
					if(!KiloImportFile(outpath, cs.directory, impmsg)) {
						Cerr() << impmsg;
						SetExitCode(2);
						return;
					}
					Cout() << impmsg;
					if(!impmsg.EndsWith("\n"))
						Cout() << "\n";
				} else
					Cout() << "import with: kilo import " << outpath << "\n";
			}
			else {
				if(FileExists(outpath) && !o.force)
					throw SessionError("output file exists: " + outpath + " (use --force)");
				String werr;
				if(!WriteClaudeJsonl(cs, outpath, werr))
					throw SessionError(werr);
				Cout() << "claude jsonl written: " << outpath << " (" << cs.messages.GetCount()
				       << " messages, " << partcount << " parts)\n";
				if(o.install_flag) {
					String installed;
					String ierr;
					if(!InstallClaudeJsonl(cs, outpath, installed, ierr))
						throw SessionError(ierr);
					Cout() << "installed: " << installed << "\n";
				}
			}
		}
	}
	catch(SessionError e) {
		Cerr() << "AgentSessionManager: " << e.message << "\n";
		SetExitCode(2);
		return;
	}
}

