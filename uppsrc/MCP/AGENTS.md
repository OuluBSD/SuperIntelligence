Scope: uppsrc/MCP

Purpose
- Headless MCP core: JSON-RPC 2.0 protocol, TCP server, framing, and in-memory logging. No IDE dependencies.

Composition
- MCP.h aggregates Protocol, Server, Log under NAMESPACE_UPP.
- Protocol.h: request/response types, errors, helpers.
- Server: client struct, framing, routing; IDE-specific handlers live in uppsrc/ide/MCP.
- Log: in-memory log buffer with levels and snapshot for GUI.

Integration
- uppsrc/ide/MCP depends on this package and adds IDE-specific handlers (workspace.*, node.*, etc.).

