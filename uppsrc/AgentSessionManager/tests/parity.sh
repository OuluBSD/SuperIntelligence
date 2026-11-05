#!/bin/bash
# AgentSessionManager verification suite.
# Reads LIVE session data (~/.claude, ~/.local/share/kilo, ~/.gemini) - read-only
# except the marked live-import test, which creates and deletes a throwaway
# Kilo session through the official `kilo import` / `kilo session delete`.
# Run from the repository root after building:
#   uppbuilder AgentSessionManager -m CLANG
#   uppsrc/AgentSessionManager/tests/parity.sh
set -u
cd "$(dirname "$0")/../../.." || exit 1
A=bin/AgentSessionManager
[ -x "$A" ] || { echo "bin/AgentSessionManager not built"; exit 1; }
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
ok=0; fail=0
cmp2() {
  local desc="$1"; shift
  ~/bin/sessions "$@" > "$TMP/py_out" 2> "$TMP/py_err"; local pe=$?
  "$A" "$@" > "$TMP/cpp_out" 2> "$TMP/cpp_err"; local ce=$?
  sed -i 's/session_tool\.py/AgentSessionManager/g; s/session-tool:/AgentSessionManager:/; s/{list,dump,state,changes,canonical,convert}/{list,dump,state,changes}/; s/'"'"'changes'"'"', '"'"'canonical'"'"', '"'"'convert'"'"'/'"'"'changes'"'"'/; s/  */ /g; s/^ //' "$TMP/py_err" "$TMP/cpp_err"
  if [ "$pe" -eq "$ce" ] && diff -q "$TMP/py_out" "$TMP/cpp_out" >/dev/null 2>&1 && diff -q "$TMP/py_err" "$TMP/cpp_err" >/dev/null 2>&1; then
    echo "PASS: $desc"; ok=$((ok+1))
  else
    echo "FAIL: $desc (py=$pe cpp=$ce)"; fail=$((fail+1))
    diff "$TMP/py_out" "$TMP/cpp_out" | head -4
    diff "$TMP/py_err" "$TMP/cpp_err" | head -4
  fi
}

echo "== phase 1: parity with ~/bin/sessions =="
# list comparison tolerates live state flips (running/waiting change between
# the two runs for actively-updated sessions): compare sorted non-state columns
~/bin/sessions list > "$TMP/py_out" 2>&1
"$A" list > "$TMP/cpp_out" 2>&1
if [ "$(cut -f1,2,5 "$TMP/py_out" | LC_ALL=C sort)" = "$(cut -f1,2,5 "$TMP/cpp_out" | LC_ALL=C sort)" ]; then
  echo "PASS: list"; ok=$((ok+1))
else
  echo "FAIL: list"; fail=$((fail+1))
  diff <(cut -f1,2,5 "$TMP/py_out" | sort) <(cut -f1,2,5 "$TMP/cpp_out" | sort) | head -4
fi
cmp2 "list kilo limit" --source kilo list --limit 5
PY_ANT=$(~/bin/sessions --source antigravity list 2>/dev/null | head -1 | cut -f2)
PY_CLA=$(~/bin/sessions --source claude list 2>/dev/null | head -1 | cut -f2)
PY_KIL=$(~/bin/sessions --source kilo list 2>/dev/null | head -1 | cut -f2)
cmp2 "dump antigravity" --source antigravity dump "$PY_ANT" --tail 3
cmp2 "dump claude" --source claude dump "$PY_CLA" --tail 3
cmp2 "dump kilo" --source kilo dump "$PY_KIL" --range 1 3
cmp2 "state" --source kilo state "$PY_KIL" --field running
cmp2 "changes baseline" --source antigravity changes "$PY_ANT"
cmp2 "bad cookie" --source antigravity changes "$PY_ANT" --cookie garbage
cmp2 "missing session" --source kilo dump no-such-id
cmp2 "bad source" --source bogus list
cmp2 "bad command" bogus
cmp2 "neg limit" --source kilo list --limit -1
cmp2 "bad range" --source kilo dump "$PY_KIL" --range 5 2
cmp2 "neg tail" --source kilo dump "$PY_KIL" --tail -3
cmp2 "bad since" --source claude dump "$PY_CLA" --since not-a-time
cmp2 "bad field" --source kilo state "$PY_KIL" --field bogus

echo "== phase 2: canonical + convert =="
"$A" --source kilo canonical "$PY_KIL" --out "$TMP/canon_kilo.json" >/dev/null 2>&1
python3 - "$TMP/canon_kilo.json" <<'EOF' && ok=$((ok+1)) || { echo "FAIL: kilo canonical"; fail=$((fail+1)); }
import json, sys
d = json.load(open(sys.argv[1]))
assert d["version"] == 1 and d["source"] == "kilo"
assert d["messages"], "no messages"
assert all(m["src_json"] for m in d["messages"])
EOF
"$A" --source kilo convert "$PY_KIL" kilo --out "$TMP/ke.json" >/dev/null 2>&1
python3 - "$TMP/ke.json" <<'EOF' && ok=$((ok+1)) || { echo "FAIL: kilo export schema"; fail=$((fail+1)); }
import json, sys
d = json.load(open(sys.argv[1]))
info = d["info"]
for k in ("id", "slug", "projectID", "directory", "title", "version", "time"):
    assert k in info, k
assert info["id"].startswith("ses")
for m in d["messages"]:
    assert m["info"]["id"].startswith("msg") and m["info"]["sessionID"] == info["id"]
    for p in m["parts"]:
        assert p["id"].startswith("prt") and p["messageID"] == m["info"]["id"]
        assert p["type"] in ("text","subtask","reasoning","file","tool","step-start","step-finish","snapshot","patch","agent","retry","compaction"), p["type"]
EOF
"$A" --source kilo convert "$PY_KIL" claude --out "$TMP/c.jsonl" >/dev/null 2>&1 && [ -s "$TMP/c.jsonl" ] && { echo "PASS: kilo->claude jsonl"; ok=$((ok+1)); } || { echo "FAIL: kilo->claude"; fail=$((fail+1)); }
"$A" --source kilo convert "$PY_KIL" kilo --out "$TMP/dry.json" --dry-run >/dev/null 2>&1
[ ! -e "$TMP/dry.json" ] && { echo "PASS: dry-run writes nothing"; ok=$((ok+1)); } || { echo "FAIL: dry-run wrote a file"; fail=$((fail+1)); }
"$A" --source kilo convert "$PY_KIL" claude --import >/dev/null 2>&1
[ $? -ne 0 ] && { echo "PASS: --import rejected for claude"; ok=$((ok+1)); } || { echo "FAIL: --import accepted for claude"; fail=$((fail+1)); }

echo "== $ok pass, $fail fail =="
[ "$fail" -eq 0 ]