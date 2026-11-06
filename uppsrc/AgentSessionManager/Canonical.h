#ifndef _AgentSessionManager_Canonical_h_
#define _AgentSessionManager_Canonical_h_

#include <Core/Core.h>

NAMESPACE_UPP

struct CanonicalPart : Moveable<CanonicalPart> {
	String type;
	String id;
	String text;
	String tool;
	String status;
	String call_id;
	String input_json;
	String output_json;
	int64  stamp_ms;
	Value  meta;
	String src_json;
	String payload_base64;
	Value  extracted_strings;

	CanonicalPart() { stamp_ms = 0; }
};

struct CanonicalMessage : Moveable<CanonicalMessage> {
	String id;
	String role;
	int64  stamp_ms;
	String src_json;
	Vector<CanonicalPart> parts;

	CanonicalMessage() { stamp_ms = 0; }
};

struct CanonicalSession {
	int    version;
	String id;
	String source;
	String title;
	String directory;
	int64  created_ms;
	int64  updated_ms;
	Value  meta;
	Vector<CanonicalMessage> messages;

	CanonicalSession()
	{
		version = 1;
		created_ms = 0;
		updated_ms = 0;
	}
};

Value  CanonicalToJson(const CanonicalSession& s);
bool   CanonicalFromJson(const Value& v, CanonicalSession& s, String& error);

String GenKiloId(const char *prefix);
String GenHexId(int n);
String GenCallId();
String GenClaudeUuid();

CanonicalSession ClaudeToCanonical(const SessionEntry& s);
CanonicalSession KiloToCanonical(const SessionEntry& s);
CanonicalSession AntigravityToCanonical(const SessionEntry& s);
CanonicalSession ToCanonical(const SessionEntry& s);

Value  ToKiloExportJson(const CanonicalSession& s, bool fresh_session_id,
	const String& model_override, String& error);
bool   WriteClaudeJsonl(const CanonicalSession& s, const String& path, String& error,
	const String& to_directory = String(), const String& session_id_override = String());
bool   KiloImportFile(const String& file, const String& cwd, String& message);
bool   InstallClaudeJsonl(const String& directory, const String& session_id, const String& path,
	String& installed_path, String& error);

END_UPP_NAMESPACE

#endif
