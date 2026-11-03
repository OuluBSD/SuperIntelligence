#include "SweAgent.h"

NAMESPACE_UPP

static ValueMap SweAgentToolJsonSchema(const ValueMap& properties, const ValueArray& required) {
	ValueMap schema;
	schema.Add("type", "object");
	schema.Add("properties", properties);
	schema.Add("required", required);
	return schema;
}

static ValueMap SweAgentStringParam(const String& description) {
	ValueMap p;
	p.Add("type", "string");
	p.Add("description", description);
	return p;
}

static ValueMap SweAgentNumberParam(const String& description) {
	ValueMap p;
	p.Add("type", "number");
	p.Add("description", description);
	return p;
}

// -----------------------------------------------------------------------------
// ShellTool
// -----------------------------------------------------------------------------
Vector<String> ShellTool::GetAliases() const {
	Vector<String> a;
	a.Add("bash");
	return a;
}

String ShellTool::GetDescription() const {
	return "Execute a shell command inside the workspace and return its output.";
}

ValueMap ShellTool::GetParametersSchema() const {
	ValueMap properties;
	properties.Add("command", SweAgentStringParam("Shell command to execute inside the workspace."));
	ValueArray required;
	required.Add("command");
	return SweAgentToolJsonSchema(properties, required);
}

Value ShellTool::Execute(const ValueMap& args, const String& workspace_dir) const {
	String command = args["command"];
	if(command.IsEmpty())
		command = args["cmd"];
	if(command.IsEmpty())
		return "Error: missing required parameter 'command'.";

	LocalProcess process;
	Vector<String> bash_args;
	bash_args.Add("-c");
	bash_args.Add(command);
	const char* cd = workspace_dir.IsEmpty() ? NULL : ~workspace_dir;
	if(!process.Start("bash", bash_args, NULL, cd))
		return "Error: failed to start shell.";

	String out;
	int exit_code = process.Finish(out);
	if(exit_code != 0)
		return "Exit code " + Format("%d", exit_code) + ":\n" + out;
	return out.IsEmpty() ? "(command completed with no output)" : out;
}

// -----------------------------------------------------------------------------
// FileReadTool
// -----------------------------------------------------------------------------
Vector<String> FileReadTool::GetAliases() const {
	Vector<String> a;
	a.Add("view");
	return a;
}

String FileReadTool::GetDescription() const {
	return "Read the contents of a file with optional line offset and limit pagination.";
}

ValueMap FileReadTool::GetParametersSchema() const {
	ValueMap properties;
	properties.Add("path", SweAgentStringParam("File path relative to workspace."));
	properties.Add("offset", SweAgentNumberParam("Line to start reading from (1-indexed)."));
	properties.Add("limit", SweAgentNumberParam("Maximum number of lines to return (default 2000)."));
	ValueArray required;
	required.Add("path");
	return SweAgentToolJsonSchema(properties, required);
}

Value FileReadTool::Execute(const ValueMap& args, const String& workspace_dir) const {
	String path = args["path"];
	if(path.IsEmpty())
		path = args["file"];
	if(path.IsEmpty())
		return "Error: missing required parameter 'path'.";

	String full_path = workspace_dir.IsEmpty() ? path : AppendFileName(workspace_dir, path);
	if(!FileExists(full_path))
		return "Error: file not found: " + path;

	String content = LoadFile(full_path);
	if(content.IsVoid())
		return "Error: could not read file: " + path;

	Vector<String> lines = Split(content, '\n', false);
	int offset = 1;
	if(!IsNull(args["offset"]))
		offset = max(1, (int)args["offset"]);
	int limit = 2000;
	if(!IsNull(args["limit"]))
		limit = max(1, (int)args["limit"]);

	int start_idx = offset - 1;
	int end_idx = min(start_idx + limit, lines.GetCount());

	StringBuffer sb;
	int long_lines = 0;
	for(int i = start_idx; i < end_idx; i++) {
		String line = lines[i];
		if(line.GetCount() > 2000) {
			long_lines++;
			line = line.Mid(0, 2000) + "… [line truncated]";
		}
		sb << line << "\n";
	}

	String result = String(sb);
	if(end_idx < lines.GetCount() || long_lines > 0) {
		result << "\n[Hint: Showing lines " << offset << "-" << end_idx
		       << " of " << lines.GetCount() << ". Use offset=" << (end_idx + 1) << " to continue.]";
	}
	return result;
}

// -----------------------------------------------------------------------------
// FileWriteTool
// -----------------------------------------------------------------------------
Vector<String> FileWriteTool::GetAliases() const {
	Vector<String> a;
	a.Add("write");
	return a;
}

String FileWriteTool::GetDescription() const {
	return "Write content to a file, creating parent directories automatically if needed.";
}

ValueMap FileWriteTool::GetParametersSchema() const {
	ValueMap properties;
	properties.Add("path", SweAgentStringParam("File path to write."));
	properties.Add("content", SweAgentStringParam("Content to write into the file."));
	ValueArray required;
	required.Add("path");
	required.Add("content");
	return SweAgentToolJsonSchema(properties, required);
}

Value FileWriteTool::Execute(const ValueMap& args, const String& workspace_dir) const {
	String path = args["path"];
	if(path.IsEmpty())
		return "Error: missing required parameter 'path'.";
	String content = args["content"];

	String full_path = workspace_dir.IsEmpty() ? path : AppendFileName(workspace_dir, path);
	RealizeDirectory(GetFileFolder(full_path));

	if(!SaveFile(full_path, content))
		return "Error: failed to write file: " + path;

	return "File written successfully: " + path;
}

// -----------------------------------------------------------------------------
// FileEditTool
// -----------------------------------------------------------------------------
Vector<String> FileEditTool::GetAliases() const {
	Vector<String> a;
	a.Add("edit");
	return a;
}

String FileEditTool::GetDescription() const {
	return "Replace text in an existing file or update its contents.";
}

ValueMap FileEditTool::GetParametersSchema() const {
	ValueMap properties;
	properties.Add("path", SweAgentStringParam("File path to edit."));
	properties.Add("old_string", SweAgentStringParam("Exact text to find and replace."));
	properties.Add("new_string", SweAgentStringParam("Replacement text."));
	properties.Add("content", SweAgentStringParam("Full content if replacing entire file."));
	ValueArray required;
	required.Add("path");
	return SweAgentToolJsonSchema(properties, required);
}

Value FileEditTool::Execute(const ValueMap& args, const String& workspace_dir) const {
	String path = args["path"];
	if(path.IsEmpty())
		path = args["file"];
	if(path.IsEmpty())
		return "Error: missing required parameter 'path'.";

	String full_path = workspace_dir.IsEmpty() ? path : AppendFileName(workspace_dir, path);

	if(!IsNull(args["content"])) {
		RealizeDirectory(GetFileFolder(full_path));
		if(!SaveFile(full_path, String(args["content"])))
			return "Error: failed to write file: " + path;
		return "File written successfully: " + path;
	}

	String old_text = args["old_string"];
	String new_text = args["new_string"];
	if(old_text.IsEmpty()) {
		RealizeDirectory(GetFileFolder(full_path));
		if(!SaveFile(full_path, new_text))
			return "Error: failed to create file: " + path;
		return "File created successfully: " + path;
	}

	if(!FileExists(full_path))
		return "Error: file not found: " + path;

	String content = LoadFile(full_path);
	if(content.IsVoid())
		return "Error: could not read file: " + path;

	int pos = content.Find(old_text);
	if(pos < 0)
		return "Error: old_string not found in file: " + path;

	content = content.Left(pos) + new_text + content.Mid(pos + old_text.GetLength());
	if(!SaveFile(full_path, content))
		return "Error: failed to save modified file: " + path;

	return "File edited successfully: " + path;
}

// -----------------------------------------------------------------------------
// ListDirTool
// -----------------------------------------------------------------------------
Vector<String> ListDirTool::GetAliases() const {
	Vector<String> a;
	a.Add("ls");
	return a;
}

String ListDirTool::GetDescription() const {
	return "List directory contents with directories ending in '/'.";
}

ValueMap ListDirTool::GetParametersSchema() const {
	ValueMap properties;
	properties.Add("path", SweAgentStringParam("Directory path to list (default: current directory)."));
	ValueArray required;
	return SweAgentToolJsonSchema(properties, required);
}

Value ListDirTool::Execute(const ValueMap& args, const String& workspace_dir) const {
	String path = args["path"];
	if(path.IsEmpty())
		path = ".";

	String full_path = workspace_dir.IsEmpty() ? path : AppendFileName(workspace_dir, path);
	if(!DirectoryExists(full_path))
		return "Error: directory not found: " + path;

	FindFile ff(AppendFileName(full_path, "*"));
	Vector<String> entries;
	while(ff) {
		String name = ff.GetName();
		if(name != "." && name != "..") {
			entries.Add(name + (ff.IsFolder() ? "/" : ""));
		}
		ff.Next();
	}
	Sort(entries);

	StringBuffer sb;
	for(int i = 0; i < entries.GetCount(); i++)
		sb << entries[i] << "\n";

	return String(sb);
}

END_UPP_NAMESPACE
