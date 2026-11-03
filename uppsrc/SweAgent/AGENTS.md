# AGENTS - SweAgent Package

## Scope
- Applies to `uppsrc/SweAgent` and references (`reference/SweAgent1`).

## Purpose
- Headless autonomous software engineering agent framework in U++.
- Strictly relies on `Core/Core.h` without any GUI dependencies.
- Extensible ReAct inner loop with LLM provider abstraction, tool interfaces, and event notifications.

## Components
- `SweAgent.h`: Package umbrella header aggregating types, tools, provider, and agent classes within `namespace Upp`.
- `Types.h`: Core data structures (`AgentConfig`, `MessageRole`, `ToolCall`, `Message`).
- `Tool.h`, `Tool.cpp`: Tool base class and standard built-in tools (`BashTool`, `ViewFileTool`, `EditFileTool`).
- `Provider.h`, `Provider.cpp`: `LlmProvider` interface and `OpenAiCompatibleProvider` (OpenRouter API support via `HttpRequest`).
- `Agent.h`, `Agent.cpp`: `Agent` ReAct loop with event callbacks (`WhenThought`, `WhenAction`, `WhenObservation`, `WhenFinished`, `WhenError`).
