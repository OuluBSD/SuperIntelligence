# AGENTS - SuperIntelligence

**Repository**: `OuluBSD/SuperIntelligence`  
**Purpose**: Professional LLM runtime, SweAgent, prompting chains, and AI Playground GUI workbench.

---

## Architectural Principles

1. **Headless / GUI Dual Purpose (`ai/rules/headless-gui-dual-purpose.md`)**:
   - `SweAgent` and `SuperIntelligence/Provider` are strictly headless and rely only on `BazaarCore` (`Core`, `Core/SSL`). They have ZERO GUI dependencies so that `OuluBSD/IDE` and command-line runners can link them directly.
   - `SuperIntelligence/Ctrl` contains the GUI presentation layer (`CompletionCtrl`, `ChatAiCtrl`, `AiSetupDlg`, `PlaygroundWindow`) and inherits from `Vfs/Ctrl` (`VfsTreeAppWindow`).

2. **AI Playground Reusable Workbench (`OuluBSD/VFS`)**:
   - Uses `OuluBSD/VFS` as the virtual filesystem and dynamic workbench tree template.
   - `CompletionThread` and `ChatThread` derive from `VfsValueExt`.
   - `CompletionCtrl` and `ChatAiCtrl` derive from `VfsValueExtCtrl`.
   - The tree on the left dynamically routes node selection to the appropriate controller via `VfsValueExtFactory`.
   - Workspaces serialize cleanly via JSON round-trip (`VfsTreeApp::SaveTree` / `LoadTree`).

---

## Package Map

| Package | Path | Type | Purpose |
|---|---|---|---|
| `SweAgent` | `uppsrc/SweAgent` | Library (Headless) | Core autonomous coding agent framework |
| `SuperIntelligence/Provider` | `uppsrc/SuperIntelligence/Provider` | Library (Headless) | Account/provider manager (`AiManager()`) |
| `SuperIntelligence/Ctrl` | `uppsrc/SuperIntelligence/Ctrl` | Library (GUI) | Completion, Chat, and Provider setup views |
| `examples/SweAgentDemo` | `examples/SweAgentDemo` | Console App | SWE-agent test suite and runner |
| `examples/Playground` | `examples/Playground` | GUI App | Standalone AI Playground workbench |
| `AgentSessionManager` | `uppsrc/AgentSessionManager` | Console App | AI agent CLI session inspector and converter (Claude, Kilo, Antigravity) |

---

## Building and Testing

```bash
# SweAgent tests
uppbuilder examples/SweAgentDemo -m CLANG
./bin/SweAgentDemo test 15

# Playground build and automated self-test
uppbuilder examples/Playground -m CLANG
./bin/Playground --test

# AgentSessionManager build and verification suite (reads live session data)
uppbuilder AgentSessionManager -m CLANG
./bin/AgentSessionManager --man
uppsrc/AgentSessionManager/tests/parity.sh
```
