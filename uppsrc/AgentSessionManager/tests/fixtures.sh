#!/bin/bash
# AgentSessionManager fixture-based verification suite.
# Builds a synthetic HOME (claude JSONL, kilo SQLite, antigravity SQLite with
# protobuf-ish payloads), then verifies list/dump/state/changes, canonical
# export, cross conversions, re-homing, and cookie cursors against exact
# expected values. Fully self-contained; safe to run in CI.
# Run from anywhere: uppsrc/AgentSessionManager/tests/fixtures.sh
set -u
A="$(cd "$(dirname "$0")/../../.." && pwd)/bin/AgentSessionManager"
[ -x "$A" ] || { echo "bin/AgentSessionManager not built"; exit 1; }
FAKE=$(mktemp -d)
if [ "${FIXTURE_KEEP:-0}" = "1" ]; then
	trap 'echo "fixture HOME kept at $FAKE"' EXIT
else
	trap 'rm -rf "$FAKE"' EXIT
fi
ok=0; fail=0
pass() { echo "PASS: $1"; ok=$((ok+1)); }
failf() { echo "FAIL: $1"; fail=$((fail+1)); }
chk() { if [ "$2" = "$3" ]; then pass "$1"; else failf "$1 (got '$2' want '$3')"; fi }
chkpy() { local desc="$1"; shift; if python3 - "$@" 2>"$FAKE/pyerr"; then pass "$desc"; else failf "$desc"; sed -n '1,6p' "$FAKE/pyerr"; fi }

############################ fixtures ############################
python3 - "$FAKE" <<'PYEOF' || { echo "fixture build failed"; exit 1; }
import json, os, sqlite3, struct, sys

home = sys.argv[1]

def iso(s):
    import datetime
    return int(datetime.datetime.fromisoformat(s.replace("Z", "+00:00")).timestamp() * 1000)

# ---- claude
cdir = os.path.join(home, ".claude/projects/-tmp-fixture-proj")
sdir = os.path.join(cdir, "subagents")
os.makedirs(sdir, exist_ok=True)
sid = "11111111-2222-3333-4444-555555555555"
records = [
    {"type": "user", "sessionId": sid, "cwd": "/tmp/fixture-proj", "timestamp": "2026-01-02T03:04:05Z",
     "uuid": "u-1", "message": {"role": "user", "content": "hello fixture"}},
    {"type": "assistant", "sessionId": sid, "cwd": "/tmp/fixture-proj", "timestamp": "2026-01-02T03:04:06Z",
     "uuid": "u-2", "message": {"role": "assistant", "content": [
        {"type": "text", "text": "hi there"},
        {"type": "tool_use", "id": "toolu_1", "name": "bash", "input": {"command": "ls"}}]}},
    {"type": "user", "sessionId": sid, "cwd": "/tmp/fixture-proj", "timestamp": "2026-01-02T03:04:07Z",
     "uuid": "u-3", "message": {"role": "user", "content": [
        {"type": "tool_result", "tool_use_id": "toolu_1", "content": "file list"}]}},
    "not-json-garbage",
    {"type": "user", "sessionId": sid, "cwd": "/tmp/fixture-proj", "timestamp": "2026-01-02T03:04:08Z",
     "uuid": "u-4", "isMeta": True, "message": {"role": "user", "content": "meta skipped"}},
    {"type": "summary", "summary": "ignored", "leafUuid": "u-1"},
]
with open(os.path.join(cdir, sid + ".jsonl"), "w") as f:
    for r in records:
        f.write(json.dumps(r, separators=(",", ":")) + "\n")
with open(os.path.join(sdir, "other.jsonl"), "w") as f:
    f.write(json.dumps({"type": "user", "sessionId": "zz", "cwd": "/x", "message": {"role": "user", "content": "should not appear"}}) + "\n")

# ---- kilo
kdb = os.path.join(home, ".local/share/kilo/kilo.db")
os.makedirs(os.path.dirname(kdb), exist_ok=True)
con = sqlite3.connect(kdb)
con.executescript("""
CREATE TABLE session (id TEXT PRIMARY KEY, title TEXT, agent TEXT, directory TEXT,
  slug TEXT, version TEXT, cost REAL, tokens_input INTEGER, tokens_output INTEGER,
  tokens_reasoning INTEGER, tokens_cache_read INTEGER, tokens_cache_write INTEGER,
  summary_additions INTEGER, summary_deletions INTEGER, summary_files INTEGER,
  model TEXT, time_created INTEGER, time_updated INTEGER);
CREATE TABLE message (id TEXT PRIMARY KEY, session_id TEXT, time_created INTEGER, data TEXT);
CREATE TABLE part (id TEXT PRIMARY KEY, message_id TEXT, session_id TEXT, time_created INTEGER, data TEXT);
""")
ksid = "ses_fixture1"
con.execute("INSERT INTO session VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
    (ksid, "Fixture kilo session", "code", "/tmp/fixture-proj", "fixture-kilo", "7.7.9",
     0.5, 10, 20, 5, 100, 0, 1, 2, 3, '{"id":"m1","providerID":"p1"}', 1767229440000, 1767229449000))
m1d = json.dumps({"role": "user", "time": {"created": 1767236645000}})
m2d = json.dumps({"role": "assistant", "time": {"created": 1767236646000, "completed": 1767236648000}})
con.execute("INSERT INTO message VALUES (?,?,?,?)", ("msg_fixture1", ksid, 1767236645000, m1d))
con.execute("INSERT INTO message VALUES (?,?,?,?)", ("msg_fixture2", ksid, 1767236646000, m2d))
p1d = json.dumps({"type": "text", "text": "kilo fixture prompt"})
tool_state = {"status": "completed", "input": {"command": "ls"}, "output": "done listing",
              "title": "bash", "metadata": {}, "time": {"start": 1767236646100, "end": 1767236646900}}
p2d = json.dumps({"type": "tool", "tool": "bash", "callID": "call_fix1", "state": tool_state})
con.execute("INSERT INTO part VALUES (?,?,?,?,?)", ("prt_fixture1", "msg_fixture1", ksid, 1767236645100, p1d))
con.execute("INSERT INTO part VALUES (?,?,?,?,?)", ("prt_fixture2", "msg_fixture2", ksid, 1767236646100, p2d))
con.commit(); con.close()

# ---- antigravity
adir = os.path.join(home, ".gemini/antigravity-cli")
os.makedirs(os.path.join(adir, "conversations"), exist_ok=True)
scon = sqlite3.connect(os.path.join(adir, "conversation_summaries.db"))
scon.execute("CREATE TABLE conversation_summaries (conversation_id TEXT, parent_conversation_id TEXT,"
             " nesting_depth INTEGER, title TEXT, preview TEXT, workspace_uris TEXT, last_modified_time TEXT)")
asid = "aaaaaaaa-1111-2222-3333-444444444444"
scon.execute("INSERT INTO conversation_summaries VALUES (?,?,?,?,?,?,?)",
    (asid, "", 0, "Fixture title", "Fixture preview", '["file:///tmp/fixture-proj"]', "2026-01-01 03:04:09"))
scon.execute("INSERT INTO conversation_summaries VALUES (?,?,?,?,?,?,?)",
    ("bbbbbbbb-1111-2222-3333-444444444444", asid, 1, "Nested", "Nested preview", "[]", "2026-01-01 03:04:10"))
scon.commit(); scon.close()

def varint(n):
    out = b""
    while True:
        b = n & 0x7f
        n >>= 7
        out += bytes([b | (0x80 if n else 0)])
        if not n:
            return out

def ld(field, payload):
    return bytes([field << 3 | 2]) + varint(len(payload)) + payload

con = sqlite3.connect(os.path.join(adir, "conversations", asid + ".db"))
con.execute("CREATE TABLE steps (idx INTEGER, step_type INTEGER, status INTEGER, step_payload BLOB)")
s0 = b"\x08" + varint(1767236649) + ld(2, b"fixture step text 0") + b"\x00" + ld(2, b"line two") + b"\x00\x28\x39"
s1 = b"\x08" + varint(1767236650) + ld(2, b"\x08" + varint(1767236650) + ld(2, b"fixture step text 1") + b"\x00-3750763034362895579") + b"\x00" + ld(2, b"1234-5678") + b"\x00" + ld(2, b"$weird")
s2 = b"\x08" + varint(1767236651) + ld(2, b"call_fixant") + b"\x00" + ld(2, b"run_command") + b"\x00" + ld(2, b'{"CommandLine":"echo fixture tool","WaitMsBeforeAsync":5,"toolSummary":"fixture"}') + b"\x00" + ld(2, b"allow") + b"\x00" + ld(2, b"fixture tool output")
s3 = b"\x08" + varint(1767236652) + ld(2, b"fixture continuation") + b"\x00" + ld(2, b"RESOURCE_EXHAUSTED (code 429): Individual quota reached") + b"\x00" + ld(2, b'"quotaResetTimeStamp": "2026-01-03T00:00:00Z"')
con.execute("INSERT INTO steps VALUES (?,?,?,?)", (0, 14, 3, s0))
con.execute("INSERT INTO steps VALUES (?,?,?,?)", (1, 15, 3, s1))
con.execute("INSERT INTO steps VALUES (?,?,?,?)", (2, 132, 5, s2))
con.execute("INSERT INTO steps VALUES (?,?,?,?)", (3, 16, 3, s3))
con.commit(); con.close()
print("fixtures built in", home)
PYEOF

export HOME="$FAKE"
export PATH="$PATH"

############################ reads ############################
echo "== fixture reads =="
"$A" list > "$FAKE/list.txt" 2>&1
chk "list row count (3 sources, subagents+nested excluded)" "$(wc -l < "$FAKE/list.txt")" "3"
chk "claude list row" "$(grep -c '^claude	11111111-2222-3333-4444-555555555555	false	false	hello fixture$' "$FAKE/list.txt")" "1"
chk "kilo list row" "$(grep -c '^kilo	ses_fixture1	false	true	Fixture kilo session$' "$FAKE/list.txt")" "1"
chk "antigravity list row" "$(grep -c '^antigravity	aaaaaaaa-1111-2222-3333-444444444444	false	false	Fixture preview$' "$FAKE/list.txt")" "1"

"$A" --source claude dump 11111111 > "$FAKE/dump_claude.txt" 2>&1
chkpy "claude dump content" "$FAKE/dump_claude.txt" <<'EOF'
import sys
t = open(sys.argv[1]).read()
assert t.startswith("# hello fixture\n\nSource: claude | Session: `11111111-2222-3333-4444-555555555555` | Directory: `/tmp/fixture-proj`\n\n"), t[:120]
assert "## User 1 - 2026-01-02T03:04:05Z\n\nhello fixture\n\n" in t
assert "## Assistant 2 - 2026-01-02T03:04:06Z\n\nhi there\n\nTool `bash`\n\n```json\n{\n  \"command\": \"ls\"\n}\n```\n\n" in t
assert "## User 3 - 2026-01-02T03:04:07Z\n\nfile list\n\n" in t
assert "meta skipped" not in t and "ignored" not in t and "should not appear" not in t
EOF

"$A" --source kilo dump ses_fixture1 > "$FAKE/dump_kilo.txt" 2>&1
chkpy "kilo dump content" "$FAKE/dump_kilo.txt" <<'EOF'
import sys
t = open(sys.argv[1]).read()
assert t.startswith("# Fixture kilo session\n\nSource: kilo | Session: `ses_fixture1` | Directory: `/tmp/fixture-proj`\n\n"), t[:120]
assert "## User 1 - 2026-01-01T03:04:05Z\n\nkilo fixture prompt\n\n" in t
assert t.endswith("**Tool: bash (completed)**\n\n```json\n{\n  \"command\": \"ls\"\n}\n```\n\ndone listing\n\n")
EOF

"$A" --source antigravity dump aaaaaaaa > "$FAKE/dump_ant.txt" 2>&1
chkpy "antigravity dump content" "$FAKE/dump_ant.txt" <<'EOF'
import sys
t = open(sys.argv[1]).read()
assert t.startswith("# Fixture preview\n\nSource: antigravity | Session: `aaaaaaaa-1111-2222-3333-444444444444` | Directory: `/tmp/fixture-proj`\n\n"), t[:140]
assert "## Step 1 (type 14, status 3) 1 - 2026-01-01T03:04:09Z\n\nfixture step text 0\n\nline two\n\n" in t
assert "## Step 2 (type 15, status 3) 2 - 2026-01-01T03:04:10Z\n\nfixture step text 1\n\n" in t
assert "## Step 4 (type 16, status 3) 4 - 2026-01-01T03:04:12Z\n\nfixture continuation\n\n7RESOURCE_EXHAUSTED (code 429): Individual quota reached\n\n-\"quotaResetTimeStamp\": \"2026-01-03T00:00:00Z\"\n\n" in t
assert "3750763034362895579" not in t and "1234-5678" not in t and "$weird" not in t
assert "(type 14, status 3)" in t and "(type 15, status 3)" in t
EOF

chk "claude state" "$("$A" --source claude state 11111111)" '{"running": false, "waiting": false}'
chk "kilo state" "$("$A" --source kilo state ses_fixture1)" '{"running": false, "waiting": true}'
chk "antigravity state" "$("$A" --source antigravity state aaaaaaaa)" '{"running": false, "waiting": false}'
chk "kilo state field" "$("$A" --source kilo state ses_fixture1 --field waiting)" "true"

"$A" --source claude changes 11111111 > "$FAKE/cookie.txt" 2>&1
chkpy "claude cookie cursor" "$FAKE/cookie.txt" <<'EOF'
import base64, json, sys
r = json.load(open(sys.argv[1]))
assert r["has_new"] is False
c = r["cookie"] + "=" * (-len(r["cookie"]) % 4)
d = json.loads(base64.urlsafe_b64decode(c))
assert d["version"] == 1 and d["source"] == "claude"
assert d["session"] == "11111111-2222-3333-4444-555555555555"
assert d["cursor"]["latest"] == ["u-4", "2026-01-02T03:04:08Z"]
EOF
COOKIE=$(python3 -c "import json,sys; print(json.load(open('$FAKE/cookie.txt'))['cookie'])")
chk "changes with own cookie" "$("$A" --source claude changes 11111111 --cookie "$COOKIE")" "{\"has_new\": false, \"cookie\": \"$COOKIE\"}"

############################ canonical + conversions ############################
echo "== fixture canonical + conversions =="
"$A" --source kilo canonical ses_fixture1 --out "$FAKE/canon_kilo.json" >/dev/null 2>&1
chkpy "kilo canonical fidelity" "$FAKE/canon_kilo.json" <<'EOF'
import json, sys
d = json.load(open(sys.argv[1]))
assert d["source"] == "kilo" and d["id"] == "ses_fixture1"
assert d["created_ms"] == 1767229440000 and d["updated_ms"] == 1767229449000
assert d["meta"]["agent"] == "code" and d["meta"]["slug"] == "fixture-kilo"
assert d["meta"]["tokens_input"] == 10 and d["meta"]["summary_files"] == 3
assert len(d["messages"]) == 2
m0, m1 = d["messages"]
assert m0["src_json"] == '{"role": "user", "time": {"created": 1767236645000}}'
assert m0["role"] == "user" and m0["stamp_ms"] == 1767236645000
assert m1["parts"][0]["type"] == "tool_use" and m1["parts"][0]["tool"] == "bash"
assert m1["parts"][0]["status"] == "completed" and m1["parts"][0]["call_id"] == "call_fix1"
assert m1["parts"][0]["output"] == "done listing"
p = m0["parts"][0]
assert p["src"] == '{"type": "text", "text": "kilo fixture prompt"}'
EOF

"$A" --source claude canonical 11111111 --out "$FAKE/canon_claude.json" >/dev/null 2>&1
chkpy "claude canonical fidelity" "$FAKE/canon_claude.json" <<'EOF'
import json, sys
d = json.load(open(sys.argv[1]))
assert d["source"] == "claude" and len(d["messages"]) == 3
assert d["messages"][0]["src_json"].startswith('{"type":"user","sessionId":"11111111')
assert d["meta"]["skipped"] == 3
assert d["messages"][1]["parts"][1]["input"] == {"command": "ls"}
assert d["messages"][1]["parts"][1]["call_id"] == "toolu_1"
EOF

"$A" --source antigravity canonical aaaaaaaa --out "$FAKE/canon_ant.json" >/dev/null 2>&1
chkpy "antigravity canonical fidelity" "$FAKE/canon_ant.json" <<'EOF'
import json, sys
d = json.load(open(sys.argv[1]))
assert d["source"] == "antigravity" and len(d["messages"]) == 4
m0, m1, m2, m3 = d["messages"]
assert m0["role"] == "user" and m0["id"] == "step-0" and m0["stamp_ms"] == 1767236649000
assert m1["role"] == "assistant" and m1["stamp_ms"] == 1767236650000
assert m2["role"] == "assistant" and m2["stamp_ms"] == 1767236651000
assert m2["parts"][0]["type"] == "tool_use" and m2["parts"][0]["call_id"] == "call_fixant"
assert m2["parts"][0]["tool"] == "run_command"
assert m2["parts"][0]["input"] == {"CommandLine":"echo fixture tool","WaitMsBeforeAsync":5,"toolSummary":"fixture"}
assert "fixture tool output" in m2["parts"][0]["output"]
assert m3["role"] == "assistant" and m3["stamp_ms"] == 1767236652000
assert m0["parts"][0]["text"] == "fixture step text 0\nline two"
assert m0["parts"][0]["meta"] == {"idx": 0, "step_type": 14, "status": 3}
assert m0["parts"][0]["payload_base64"]
assert len(__import__('base64').b64decode(m0["parts"][0]["payload_base64"])) > 0
assert m0["parts"][0]["extracted_strings"] == ["fixture step text 0", "line two"]
assert "RESOURCE_EXHAUSTED" not in m1["parts"][0]["text"]
assert m3["parts"][0]["meta"]["quota_error"] is True
assert "Quota reset: 2026-01-03T00:00:00Z" in m3["parts"][0]["text"]
EOF

"$A" --source kilo convert ses_fixture1 claude --out "$FAKE/k2c.jsonl" >/dev/null 2>&1
mkdir -p "$FAKE/.claude/projects/-tmp-fixture-proj"
K2C_SID=$(python3 -c 'import json,sys; print(json.loads(open(sys.argv[1]).readline())["sessionId"])' "$FAKE/k2c.jsonl")
cp "$FAKE/k2c.jsonl" "$FAKE/.claude/projects/-tmp-fixture-proj/$K2C_SID.jsonl"
"$A" --source claude canonical "$K2C_SID" --out "$FAKE/canon_k2c.json" >/dev/null 2>&1
chkpy "kilo->claude->canonical cross round trip" "$FAKE/canon_k2c.json" <<'EOF'
import json, sys
d = json.load(open(sys.argv[1]))
assert d["source"] == "claude" and len(d["messages"]) == 3
m0, m1, m2 = d["messages"]
assert m0["role"] == "user" and m0["parts"][0]["type"] == "text"
assert m0["parts"][0]["text"] == "kilo fixture prompt"
assert m0["stamp_ms"] == 1767236645000
assert m1["role"] == "assistant"
assert json.loads(m1["src_json"])["message"]["stop_reason"] == "tool_use"
blocks = m1["parts"]
assert blocks[0]["type"] == "tool_use" and blocks[0]["tool"] == "bash"
assert blocks[0]["input"] == {"command": "ls"}
assert blocks[0]["call_id"].startswith("toolu_")
assert m2["role"] == "user" and m2["parts"][0]["type"] == "tool_result"
assert m2["parts"][0]["text"] == "done listing"
assert m2["parts"][0]["call_id"] == blocks[0]["call_id"]
EOF

"$A" --source claude convert 11111111 kilo --out "$FAKE/c2k.json" >/dev/null 2>&1
chkpy "claude->kilo export content" "$FAKE/c2k.json" <<'EOF'
import json, sys
d = json.load(open(sys.argv[1]))
info = d["info"]
assert info["id"].startswith("ses") and info["directory"] == "/tmp/fixture-proj"
assert info["title"] == "hello fixture [from claude 11111111-2222-3333-4444-555555555555]"
assert len(d["messages"]) == 3
m1 = d["messages"][1]
assert m1["info"]["role"] == "assistant" and m1["info"]["parentID"]
tool = [p for p in m1["parts"] if p["type"] == "tool"]
assert len(tool) == 1 and tool[0]["tool"] == "bash" and tool[0]["callID"] == "toolu_1"
assert tool[0]["state"]["status"] == "completed"
assert tool[0]["state"]["input"] == {"command": "ls"}
tool = [p for p in m1["parts"] if p["type"] == "tool"]
assert tool[0]["state"]["output"] == "file list"
assert d["messages"][2]["parts"] == []
EOF

"$A" --source antigravity convert aaaaaaaa claude --out "$FAKE/a2c.jsonl" >/dev/null 2>&1
chkpy "antigravity->claude native envelope/thread/error mapping" "$FAKE/a2c.jsonl" <<'EOF'
import json, re, sys
lines = [json.loads(l) for l in open(sys.argv[1]) if l.strip()]
assert len(lines) == 5
uuids = [r["uuid"] for r in lines]
assert all(re.fullmatch(r"[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}", u) for u in uuids)
assert all(r["parentUuid"] == (None if i == 0 else uuids[i-1]) for i,r in enumerate(lines))
for r in lines:
    for k in ("isSidechain", "userType", "entrypoint", "version", "session_id", "slug"):
        assert k in r, k
assert lines[0]["message"]["role"] == "user"
tool_call = lines[2]["message"]["content"][0]
tool_result = lines[3]["message"]["content"][0]
assert tool_call["type"] == "tool_use" and tool_result["type"] == "tool_result"
assert tool_result["tool_use_id"] == tool_call["id"]
assert lines[2]["message"]["stop_reason"] == "tool_use"
assert tool_call["name"] == "run_command" and tool_call["input"]["CommandLine"] == "echo fixture tool"
assert "fixture tool output" in tool_result["content"]
assistant = lines[4]
for k in ("id", "model", "role", "stop_details", "stop_reason", "stop_sequence", "type", "usage", "content"):
    assert k in assistant["message"], k
assert assistant["message"]["model"] == "<synthetic>"
assert assistant["message"]["usage"]["input_tokens"] == 0
assert assistant["error"] == "rate_limit" and assistant["isApiErrorMessage"] is True and assistant["apiErrorStatus"] == 429
assert "RESOURCE_EXHAUSTED" in assistant["message"]["content"][0]["text"]
EOF

"$A" --source antigravity convert aaaaaaaa kilo --out "$FAKE/a2k.json" >/dev/null 2>&1
chkpy "antigravity->kilo per-step messages/tool parts/raw payload" "$FAKE/a2k.json" <<'EOF'
import json, sys
d = json.load(open(sys.argv[1])); msgs = d["messages"]
assert len(msgs) == 4 and msgs[0]["info"]["role"] == "user"
u, a1, a2, a3 = msgs
assert all(m["info"]["role"] == "assistant" for m in (a1,a2,a3))
assert all(m["info"]["parentID"] == u["info"]["id"] for m in (a1,a2,a3))
assert a2["info"]["modelID"] == "m1" and a2["info"]["providerID"] == "p1"
assert a1["parts"][0]["type"] == "text" and a1["parts"][0]["metadata"]["idx"] == 1
assert a1["parts"][0]["metadata"]["payload_base64"]
tool = a2["parts"][0]
assert tool["tool"] == "run_command" and tool["callID"] == "call_fixant"
assert tool["state"]["input"]["CommandLine"] == "echo fixture tool"
assert "fixture tool output" in tool["state"]["output"]
assert a3["parts"][0]["metadata"]["idx"] == 3 and a3["parts"][0]["text"].startswith("Antigravity API quota error (HTTP 429")
assert a3["info"]["finish"] == "stop"
EOF

"$A" --source antigravity convert aaaaaaaa kilo --model openai/gpt-test --out "$FAKE/a2k-model.json" >/dev/null 2>&1
chkpy "explicit Kilo model override" "$FAKE/a2k-model.json" <<'EOF'
import json, sys
d = json.load(open(sys.argv[1]))
assert d["info"]["model"]["providerID"] == "openai"
assert d["info"]["model"]["id"] == "gpt-test"
assert d["info"]["metadata"]["syntheticUsage"] is True
assert d["info"]["metadata"]["syntheticModel"] is False
EOF

"$A" --source claude convert 11111111 claude --to-directory /tmp/rehomed-proj --out "$FAKE/rehome.jsonl" >/dev/null 2>&1
chkpy "claude re-home" "$FAKE/rehome.jsonl" <<'EOF'
import json, sys
lines = [json.loads(l) for l in open(sys.argv[1]) if l.strip()]
sids = {r["sessionId"] for r in lines}
assert len(sids) == 1 and "11111111-2222-3333-4444-555555555555" not in sids
assert {r["cwd"] for r in lines} == {"/tmp/rehomed-proj"}
assert lines[0]["message"]["content"] == "hello fixture"
assert lines[1]["message"]["content"][1]["input"] == {"command": "ls"}
EOF

"$A" --source kilo convert ses_fixture1 kilo --out "$FAKE/k2k.json" >/dev/null 2>&1
chkpy "kilo->kilo export data fidelity" "$FAKE/k2k.json" <<'EOF'
import json, sys
d = json.load(open(sys.argv[1]))
assert d["info"]["id"].startswith("ses") and d["info"]["id"] != "ses_fixture1"
assert d["info"]["slug"] == "fixture-kilo" and d["info"]["agent"] == "code"
assert d["info"]["tokens"]["input"] == 10 and d["info"]["cost"] == 0.5
m = d["messages"]
assert m[0]["info"]["id"].startswith("msg") and m[0]["info"]["id"] != "msg_fixture1"
assert m[0]["info"]["role"] == "user" and m[0]["info"]["time"]["created"] == 1767236645000
p = m[0]["parts"][0]
assert p["id"].startswith("prt") and p["sessionID"] == d["info"]["id"]
assert p["messageID"] == m[0]["info"]["id"]
assert p["type"] == "text" and p["text"] == "kilo fixture prompt"
tool = m[1]["parts"][0]
assert tool["type"] == "tool" and tool["state"]["output"] == "done listing"
assert tool["state"]["time"]["start"] == 1767236646100
assert m[1]["info"]["parentID"] == m[0]["info"]["id"]
assert m[1]["info"]["time"]["completed"] == 1767236648000
EOF

"$A" --source antigravity convert aaaaaaaa kilo --out "$FAKE/id-a.json" >/dev/null 2>&1
"$A" --source antigravity convert aaaaaaaa kilo --out "$FAKE/id-b.json" >/dev/null 2>&1
chkpy "generated Kilo ids unique across processes" "$FAKE/id-a.json" "$FAKE/id-b.json" <<'EOF'
import json, sys
a=json.load(open(sys.argv[1])); b=json.load(open(sys.argv[2]))
assert a["info"]["id"] != b["info"]["id"]
assert {m["info"]["id"] for m in a["messages"]}.isdisjoint({m["info"]["id"] for m in b["messages"]})
assert {p["id"] for m in a["messages"] for p in m["parts"]}.isdisjoint({p["id"] for m in b["messages"] for p in m["parts"]})
EOF

echo "== $ok pass, $fail fail =="
[ "$fail" -eq 0 ]
