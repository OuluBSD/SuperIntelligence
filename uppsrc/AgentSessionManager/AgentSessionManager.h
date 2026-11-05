#ifndef _AgentSessionManager_AgentSessionManager_h_
#define _AgentSessionManager_AgentSessionManager_h_

#include <Core/Core.h>

#include "SessionModel.h"
#include "SqliteRo.h"
#include "Canonical.h"

NAMESPACE_UPP

Vector<SessionEntry> ClaudeEnumerate();
Vector<MessageEntry> ClaudeMessages(const SessionEntry& s);
SessionState         ClaudeState(const SessionEntry& s);
Value                ClaudeCursor(const SessionEntry& s);
Vector<String>       ClaudeJsonlPaths();
Value                ClaudeSignature();

Vector<SessionEntry> KiloEnumerate();
Vector<MessageEntry> KiloMessages(const SessionEntry& s);
SessionState         KiloState(const SessionEntry& s);
Value                KiloCursor(const SessionEntry& s);
Value                KiloSignature();

Vector<SessionEntry> AntigravityEnumerate();
Vector<MessageEntry> AntigravityMessages(const SessionEntry& s);
SessionState         AntigravityState(const SessionEntry& s);
Value                AntigravityCursor(const SessionEntry& s);
Value                AntigravitySignature();

Vector<SessionEntry> AllSessions(const String& source);
Vector<SessionEntry> CachedSessions(const String& source);
SessionEntry         FindSession(const String& source, const String& id);
String               EncodeCookie(const SessionEntry& s);
bool                 CheckCookie(const SessionEntry& s, const String& cookie);
Value                SessionCursor(const SessionEntry& s);

END_UPP_NAMESPACE

#endif
