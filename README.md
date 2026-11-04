# SuperIntelligence

Professional LLM runtime, SweAgent, prompting chains, and agent architectures for U++.

## Architecture & Separation of Concerns

- **`OuluBSD/SuperIntelligence`**: Professional LLM systems, prompting engines, tool execution loops, reasoning streams, and autonomous coding agents (`SweAgent`).
- **`OuluBSD/ArtificialIntelligence`**: Classical AI systems (A*, action planner, decision trees, heuristic search, `AICtrl`).
- **`OuluLinux/ConvNetCpp`**: Early neural networks and experimental local models.

## Packages

- **`uppsrc/SweAgent`**: Headless SWE-agent framework supporting OpenRouter, Anthropic, OpenAI, DeepSeek, and custom LLM endpoints with in-process C++ event callbacks (`WhenReasoning`, `WhenAction`, `WhenObservation`, `WhenFinished`). Strictly headless (zero GUI dependencies) for direct inclusion in headless pipelines and IDEs.
- **`uppsrc/SuperIntelligence/Provider`**: Headless AI service provider configuration and manager (`AiServiceProviderManager`, `AiManager()`) with JSON persistence.
- **`uppsrc/SuperIntelligence/Ctrl`**: AI Playground workbench controls (`CompletionCtrl`, `ChatAiCtrl`, `AiSetupDlg`, `PlaygroundWindow`) inheriting the `VfsTreeAppWindow` template from `OuluBSD/VFS`.
- **`examples/Playground`**: Standalone AI Playground application featuring tree-driven multi-session text completion and chat workbench.

## Building with UppBuilder

```bash
# Build headless SweAgentDemo
uppbuilder examples/SweAgentDemo --bazaarcore ../BazaarCore -v

# Build standalone AI Playground workbench
uppbuilder examples/Playground --bazaarcore ../BazaarCore --vfs ../VFS -v
```

## Running

```bash
# Run headless SweAgent tests
./bin/SweAgentDemo test 15
./bin/SweAgentDemo test 16

# Run Playground automated self-test
./bin/Playground --test
```
