# SuperIntelligence

Professional LLM runtime, SweAgent, prompting chains, and agent architectures for U++.

## Architecture & Separation of Concerns

- **`OuluBSD/SuperIntelligence`**: Professional LLM systems, prompting engines, tool execution loops, reasoning streams, and autonomous coding agents (`SweAgent`).
- **`OuluBSD/ArtificialIntelligence`**: Classical AI systems (A*, action planner, decision trees, heuristic search, `AICtrl`).
- **`OuluLinux/ConvNetCpp`**: Early neural networks and experimental local models.

## Packages

- **`uppsrc/SweAgent`**: Headless SWE-agent framework supporting OpenRouter, Anthropic, OpenAI, DeepSeek, and custom LLM endpoints with in-process C++ event callbacks (`WhenReasoning`, `WhenAction`, `WhenObservation`, `WhenFinished`).

## Building with UppBuilder

```bash
uppbuilder examples/SweAgentDemo --bazaarcore ../BazaarCore -v
```

## Running

```bash
./bin/SweAgentDemo test 15
./bin/SweAgentDemo test 16
```
