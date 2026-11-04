# AGENTS - SuperIntelligence/Provider

**Package**: `SuperIntelligence/Provider`  
**Type**: Library (Headless)  
**Dependencies**: `Core`, `Core/SSL` (Zero GUI dependencies)

## Purpose
`SuperIntelligence/Provider` manages AI service provider accounts, credentials, URL endpoint normalization (OpenAI, Ollama, vLLM, Anthropic, Gemini), request payloads, response parsers, logprob distribution structures, and Server-Sent Events (SSE) streaming chunks.

## Key Types
- `AiServiceProvider`: Account settings, base URL normalization (`NormalizeBaseUrl`, `NormalizeEndpoint`, `GetCompletionEndpoint`, `GetChatEndpoint`), `/v1/completions` request builder (`BuildCompletionPayload`), response parser (`ParseCompletionResponse`), logprob extractor (`ParseTokenLogprobs`), SSE parser (`ParseSSEChunk`), and HTTP execution (`ExecuteCompletion`).
- `AiServiceProviderManager` / `AiManager()`: Global provider store and JSON file persistence (`ai_providers.json`).
- `TokenCandidate`: Moveable candidate token and associated logprob.
- `TokenLogprob`: Moveable token, logprob, and vector of top alternative candidate logprobs.
- `CompletionRequest`: Legacy completion parameters (`prompt`, `model`, `max_tokens`, `temperature`, `top_p`, `stop`, `logprobs`, `stream`).
- `CompletionResponse`: Legacy completion result with choices, finish reasons, token logprobs, and token usage metrics.
- `SSEEvent`: Server-sent event record (`event`, `data`, `is_done`).
