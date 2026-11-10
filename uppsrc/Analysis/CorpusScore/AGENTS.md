# CorpusScore

Scope
- Applies to `uppsrc/Analysis/CorpusScore`.

Purpose
- Core-only, headless CLI and library for building text statistics from a caller-supplied
  personal corpus and scoring candidate lines against the derived tables.
- The corpus is read-only input. Generated tables are disposable derived data.

Rules
- Keep `CorpusScore.upp` Core-only and free of `flagGUI` dependencies.
- Every implementation file includes `CorpusScore.h` first.
- The builder and scorer use the same `CorpusModel::Tokenize` implementation. Do not
  duplicate the tokenizer in another language or source file.
- Treat an incompatible `format_version` as an invariant violation with `ASSERT`.
- Use `--file` for reliable UTF-8 candidate input on Windows.

Build
- Run `bin/build.exe --list-mainconfigs CorpusScore` first.
- Build with `bin/build.exe -M <index> -m MSVS26x64 -j <jobs> CorpusScore`.
- Current implementation status: built and exercised with mainconfig `0` and
  `MSVS26x64`. The UTF-8 fixture corpus and candidate file under `testdata/` provide
  a smoke test; a full caller-supplied corpus build also verified that table counts,
  baseline metrics, and candidate scores remain finite.
