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
  antigravity steps get structured `step` parts (role: step_type 14 -> user).
  Canonical keeps stringless antigravity steps that `dump` filters out.
- `Convert.cpp`: `ToKiloExportJson` (kilo-import-compatible JSON),
  `WriteClaudeJsonl`, `KiloImportFile` (runs official `kilo import`).
- CLI: `canonical SESSION [--out FILE|-]` and
  `convert SESSION TARGET [--out FILE] [--emit-canonical F] [--from-canonical F]
  [--force] [--dry-run] [--import] [--keep-id]`.

### Kilo interchange rules (learned from Kilo-Org/kilocode@7.7.9 source)
- The live kilo DB is event-sourced; NEVER write it directly. The only write
  path is `kilo import` (writes projection tables via its own ORM).
- `kilo import` upserts the session by id and RE-HOMES it to the cwd of the
  import run; messages/parts insert with `onConflictDoNothing`, so id
  collisions are silently skipped.
- Therefore converted kilo exports ALWAYS regenerate message/part ids, and
  `--import` uses a fresh session id unless `--keep-id`. Run import with
  cwd = session directory so it lands in the right project.
- Required schema fields for kilo export JSON are filled with defaults
  (agent "code", model unknown, mode build, cost/tokens 0, tool state
  completed with time start/end).

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
