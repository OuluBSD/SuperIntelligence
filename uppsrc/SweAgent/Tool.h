#ifndef _SweAgent_Tool_h_
#define _SweAgent_Tool_h_

class Tool : public Moveable<Tool> {
public:
	virtual ~Tool() {}

	virtual String GetName() const = 0;
	virtual Vector<String> GetAliases() const { return Vector<String>(); }
	virtual String GetDescription() const = 0;
	virtual ValueMap GetParametersSchema() const = 0;
	virtual Value Execute(const ValueMap& args, const String& workspace_dir) const = 0;
};

// OpenRouter shell tool (alias: bash)
class ShellTool : public Tool {
public:
	virtual String GetName() const override { return "shell"; }
	virtual Vector<String> GetAliases() const override;
	virtual String GetDescription() const override;
	virtual ValueMap GetParametersSchema() const override;
	virtual Value Execute(const ValueMap& args, const String& workspace_dir) const override;
};
typedef ShellTool BashTool;

// OpenRouter file_read tool (alias: view)
class FileReadTool : public Tool {
public:
	virtual String GetName() const override { return "file_read"; }
	virtual Vector<String> GetAliases() const override;
	virtual String GetDescription() const override;
	virtual ValueMap GetParametersSchema() const override;
	virtual Value Execute(const ValueMap& args, const String& workspace_dir) const override;
};
typedef FileReadTool ViewFileTool;

// OpenRouter file_write tool (alias: write)
class FileWriteTool : public Tool {
public:
	virtual String GetName() const override { return "file_write"; }
	virtual Vector<String> GetAliases() const override;
	virtual String GetDescription() const override;
	virtual ValueMap GetParametersSchema() const override;
	virtual Value Execute(const ValueMap& args, const String& workspace_dir) const override;
};

// OpenRouter file_edit tool (alias: edit)
class FileEditTool : public Tool {
public:
	virtual String GetName() const override { return "file_edit"; }
	virtual Vector<String> GetAliases() const override;
	virtual String GetDescription() const override;
	virtual ValueMap GetParametersSchema() const override;
	virtual Value Execute(const ValueMap& args, const String& workspace_dir) const override;
};
typedef FileEditTool EditFileTool;

// OpenRouter list_dir tool (alias: ls)
class ListDirTool : public Tool {
public:
	virtual String GetName() const override { return "list_dir"; }
	virtual Vector<String> GetAliases() const override;
	virtual String GetDescription() const override;
	virtual ValueMap GetParametersSchema() const override;
	virtual Value Execute(const ValueMap& args, const String& workspace_dir) const override;
};

#endif
