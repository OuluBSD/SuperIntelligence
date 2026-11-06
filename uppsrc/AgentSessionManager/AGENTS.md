# AGENTS - AgentSessionManager Package

## Purpose
- U++ console main program replicating the behavior of `~/bin/sessions`
  (`~/Dev/Manager/pysrc/session_tool.py`).
- Lists, dumps, and monitors AI agent CLI sessions from three sources:
  - `claude`: Claude Code JSONL files under `~/.claude/projects`
  - `kilo`: Kilo sessions in `~/.local/share/kilo/kilo.db` (SQLite)
  - `antigravity`: Antigravity CLI conversations under `~/.gemini`
- Converts sessions between agents via a canonical intermediate format.

## Conversion (phase 2)

Pipeline: `source (read-only) -> CanonicalSession IR -> target writer`.

- `Canonical.h/.cpp`: IR (`CanonicalSession`/`CanonicalMessage`/`CanonicalPart`),
  canonical JSON v1 (de)serialization, id generators
  (`ses_`/`msg_`/`prt_`, `call_`, claude uuid). NOTE: this fork's `Vector`
  is pick-only - build IR via in-place `Add()` references, never copy.
- `CanonicalSource.cpp`: per-source readers. Kilo source stores verbatim
  `message.data`/`part.data` as `src_json`; claude stores whole record lines;
  antigravity steps get structured `step` parts (role: step_type 14 -> user),
  extracted strings, and Base64 raw protobuf payload. Canonical keeps
  stringless antigravity steps that `dump` filters out.
- `Convert.cpp`: `ToKiloExportJson` (kilo-import-compatible JSON),
  `WriteClaudeJsonl`, `KiloImportFile` (runs official `kilo import`).
- Kilo conversion emits one Kilo message per Antigravity step: type-14 steps
  are user messages; assistant steps are parented to the active user turn.
  Each message carries one text/tool part with step metadata and a native
  assistant `finish`. Kilo native export re-import comparison preserves all
  21696 messages, roles, part IDs, part types, and content (only the expected
  destination directory/projectID are re-homed). Strongly
  identified tool calls (call id + tool name + valid JSON input) become native
  Kilo/Claude tool parts; ambiguous strings stay text. It resolves a model from
  a native Kilo session in the destination project, or accepts `--model
  PROVIDER/MODEL`; otherwise it warns and marks model/usage metadata synthetic.
- Generated Claude records use UUID ids, a `parentUuid` chain, native-shaped
  user/assistant envelopes, paired tool calls/results, `stop_reason: tool_use`
  for tool calls, and error metadata for recognizable 429 quota
  errors. Antigravity text remains best-effort: the payload is opaque protobuf;
  quota errors are summarized (malformed fragments are not rendered), and
  exact payload bytes/extracted strings remain in canonical `payload_base64`
  and Kilo text/tool-part metadata.
- CLI: `canonical SESSION [--out FILE|-]` and
  `convert SESSION TARGET [--out FILE] [--emit-canonical F] [--from-canonical F]
  [--force] [--dry-run] [--import] [--install] [--keep-id] [--to-directory DIR]`.
- `--to-directory` re-homes a conversion: claude records get a fresh session id
  and the new cwd (uuid/parentUuid chains preserved); `--import` lands the kilo
  session in that project. claude-source re-homes regenerate the session id;
  antigravity/kilo sources keep their ids unless claude re-home.
- Tests: `tests/parity.sh` (live data, read-only + one approved kilo import
  round trip) and `tests/fixtures.sh` (synthetic HOME, self-contained, CI-safe:
  exact-value checks for list/dump/state/changes/cookie cursors, canonical
  fidelity incl. verbatim src payloads, cross round trips, re-home).

### Kilo interchange rules (learned from Kilo-Org/kilocode@7.7.9 source)
- The live kilo DB is event-sourced; NEVER write it directly. The only write
  path is `kilo import` (writes projection tables via its own ORM).
- `kilo import` upserts the session by id and RE-HOMES it to the cwd of the
  import run; messages/parts insert with `onConflictDoNothing`, so id
  collisions are silently skipped.
- Therefore converted kilo exports ALWAYS regenerate message/part ids, and
  `--import` uses a fresh session id unless `--keep-id`. Run import with
  cwd = session directory so it lands in the right project.
- Required schema fields for kilo export JSON are filled with defaults.
  The target project model/agent is resolved from an existing Kilo session or
  accepted via `--model`; token/cost counters remain marked synthetic zeroes
  when the source cannot provide them.

### Backups before live-import work
- /win/kilo-db-backups/kilo-20261105-173319.db
- /yhteinen/active/sblo/Backup/kilo-20261105-173319.db
(both 2.95 GiB, `PRAGMA quick_check` ok; created with `sqlite3 .backup`
while the DB was live)

## Invariants
- NEVER open session SQLite databases in read-write mode. All access goes through
  `SqliteRo` (`sqlite3_open_v2` with `SQLITE_OPEN_READONLY|SQLITE_OPEN_URI`).
- Output and exit codes must match the Python tool: exit 0 success, 2 on
  invalid input / missing or ambiguous session id / database errors
  (`AgentSessionManager: <message>` on stderr).
- The listing cache is shared with the Python tool
  (`~/.cache/session-tool/sessions.json`); signatures must follow
  `session_tool.py` exactly (mtime-ns, size, sha256 for files <= 8 MiB,
  5-minute granularity for the Kilo database).

## Components
- `AgentSessionManager.h`: umbrella header aggregating the package interface.
- `SessionModel.h/.cpp`: `SessionEntry`, `MessageEntry`, `SessionState`,
  ISO-8601 timestamp parse/format, JSON writer, Claude content extraction,
  file signatures (mtime-ns/size/sha256).
- `SqliteRo.h/.cpp`: read-only SQLite connection and prepared statement wrapper.
- `ClaudeSource.cpp`: JSONL session enumeration, messages, state, cursor.
- `KiloSource.cpp`: kilo.db enumeration, message/part rendering, state, cursor.
- `AntigravitySource.cpp`: summaries database, metadata fallback, conversation
  steps, protobuf varint timestamp heuristic, state, cursor.
- `SessionStore.cpp`: enumeration dispatch, session id prefix matching,
  signature-based cache, cookie encoding/checking.
- `AgentSessionManager.cpp`: CLI argument parsing, commands, manual text.

## Build
```bash
uppbuilder AgentSessionManager -m CLANG
./bin/AgentSessionManager --man
```

## References
- Python reference implementation: `~/Dev/Manager/pysrc/session_tool.py`
- Plan: `plan/uppsrc/AgentSessionManager/001-implementation/`

## Known deviations from the Python tool (intentional)
- Diagnostics and argparse-style usage blocks use the program name
  `AgentSessionManager` (structure, wording, and exit codes match).
- `--help` prints general help for every command; `--man` has full options.
- Numeric signature values are compared semantically (`SameValue`), because
  U++ JSON parsing stores every number as double and large mtime-ns values
  would otherwise never compare equal.
