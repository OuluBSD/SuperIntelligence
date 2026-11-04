#include "Ctrl.h"

NAMESPACE_UPP

class MockSweProvider : public LlmProvider {
public:
	int step = 0;
	String task_name;

	MockSweProvider(const String& task = "") : task_name(task) {}

	virtual bool ChatCompletion(const Vector<Message>& messages,
	                            const Vector<Tool*>& tools,
	                            const String& model,
	                            Message& response_msg) override
	{
		step++;
		if (step == 1) {
			response_msg.role = MessageRole::ASSISTANT;
			response_msg.reasoning = "Analyzing task and inspecting repository structure.";
			response_msg.content = "I will list the workspace directory to inspect available files.";
			ToolCall& tc = response_msg.tool_calls.Add();
			tc.id = "call_mock_1";
			tc.name = "list_dir";
			tc.arguments = "{\"path\":\".\"}";
			return true;
		}
		else {
			response_msg.role = MessageRole::ASSISTANT;
			response_msg.reasoning = "Task completed successfully.";
			response_msg.content = "SWE task completed: workspace analyzed and verified.";
			return true;
		}
	}

	virtual bool ChatCompletion(const Vector<Message>& messages,
	                            const String& model,
	                            String& content_result) override
	{
		content_result = "SWE task completed: workspace analyzed and verified.";
		return true;
	}

	virtual bool LoadApiKey() override { return true; }
	virtual Vector<String> GetAllowedModels() override {
		Vector<String> m;
		m.Add("mock-model");
		return m;
	}
};

ChatThread::ChatThread(VfsValue& n) : VfsValueExt(n) {
}

ChatThread::~ChatThread() {
}

void ChatThread::Visit(Vis& v) {
	v
		("model_name", model_name)
		("system_instruction", system_instruction)
		("temperature", temperature)
		("swe_mode", swe_mode)
		("workspace_dir", workspace_dir)
		("max_steps", max_steps)
		("messages", messages, 0);
}

ChatAiCtrl::ChatAiCtrl() {
	CtrlLayout(*this);

	model_name.Add("gpt-4o", "OpenAI: GPT-4o");
	model_name.Add("gpt-4-turbo", "OpenAI: GPT-4 Turbo");
	model_name.Add("claude-3-5-sonnet", "Anthropic: Claude 3.5 Sonnet");
	model_name.Add("gemini-1.5-pro", "Google: Gemini 1.5 Pro");
	model_name.Add("llama3", "Ollama: LLaMA 3");

	temperature.MinMax(0.0, 2.0);
	temperature.SetInc(0.05);

	max_steps.MinMax(1, 100);
	max_steps.SetData(10);
	swe_mode.SetData(1);
	workspace_dir.SetData(String("."));

	messages.AddColumn("Role");
	messages.AddColumn("Message");
	messages.ColumnWidths("70 300");

	model_name.WhenAction = THISBACK(SyncToThread);
	system_instruction.WhenAction = THISBACK(SyncToThread);
	temperature.WhenAction = THISBACK(SyncToThread);
	swe_mode.WhenAction = THISBACK(SyncToThread);
	workspace_dir.WhenAction = THISBACK(SyncToThread);
	max_steps.WhenAction = THISBACK(SyncToThread);

	send.WhenAction = THISBACK(SendMessage);
	clear_chat.WhenAction = THISBACK(ClearMessages);
}

ChatAiCtrl::~ChatAiCtrl() {
}

void ChatAiCtrl::Data() {
	if (!ext) return;
	ChatThread& th = GetExt<ChatThread>();

	model_name.SetData(th.model_name);
	system_instruction.SetData(th.system_instruction);
	temperature.SetData(th.temperature);
	swe_mode.SetData(th.swe_mode);
	workspace_dir.SetData(th.workspace_dir);
	max_steps.SetData(th.max_steps);

	messages.Clear();
	for (int i = 0; i < th.messages.GetCount(); i++) {
		const auto& m = th.messages[i];
		messages.Add(m.role, m.content);
	}
	if (messages.GetCount() > 0)
		messages.SetCursor(messages.GetCount() - 1);
}

void ChatAiCtrl::SyncToThread() {
	if (!ext) return;
	ChatThread& th = GetExt<ChatThread>();

	th.model_name = model_name.GetData();
	th.system_instruction = system_instruction.GetData();
	th.temperature = temperature.GetData();
	th.swe_mode = swe_mode.GetData();
	th.workspace_dir = workspace_dir.GetData();
	th.max_steps = max_steps.GetData();

	WhenEditorChange();
}

void ChatAiCtrl::AddMessage(const String& role, const String& content) {
	if (ext) {
		ChatThread& th = GetExt<ChatThread>();
		th.messages.Add(ChatThread::Message(role, content));
	}
	messages.Add(role, content);
	if (messages.GetCount() > 0)
		messages.SetCursor(messages.GetCount() - 1);
	WhenEditorChange();
}

void ChatAiCtrl::SendMessage() {
	SyncToThread();
	if (!ext) return;
	ChatThread& th = GetExt<ChatThread>();

	String user_text = TrimBoth((String)input.GetData());
	if (user_text.IsEmpty()) return;

	input.SetData("");
	AddMessage("user", user_text);

	if (th.swe_mode) {
		ExecuteSweTask(user_text);
	}
	else {
		// Regular direct chat completion (or mock reply)
		String ai_reply;
		AiServiceProvider* def_p = AiManager().GetDefaultProvider();
		if (def_p && !def_p->token.IsEmpty()) {
			CompletionRequest req;
			req.prompt = th.system_instruction + "\nUser: " + user_text + "\nAssistant:";
			req.model = th.model_name;
			req.temperature = th.temperature;
			CompletionResponse resp;
			if (def_p->ExecuteCompletion(req, resp) && !resp.choices.IsEmpty()) {
				ai_reply = TrimBoth(resp.choices[0].text);
			}
		}
		if (ai_reply.IsEmpty()) {
			ai_reply = "Assistant reply to: " + user_text;
		}
		AddMessage("assistant", ai_reply);
	}
}

void ChatAiCtrl::ExecuteSweTask(const String& task) {
	if (!ext) return;
	ChatThread& th = GetExt<ChatThread>();

	if (th.messages.IsEmpty() || th.messages.Top().content != task) {
		AddMessage("user", task);
	}

	Agent agent(th.max_steps);
	agent.config.workspace_dir = th.workspace_dir.IsEmpty() ? "." : th.workspace_dir;
	agent.config.model_name = th.model_name;
	agent.config.system_prompt = th.system_instruction;

	agent.AddTool(new ShellTool);
	agent.AddTool(new FileReadTool);
	agent.AddTool(new FileWriteTool);
	agent.AddTool(new FileEditTool);
	agent.AddTool(new ListDirTool);

	agent.WhenThought = [this](const String& thought) {
		AddMessage("thought", thought);
	};
	agent.WhenAction = [this](const String& name, const ValueMap& args) {
		AddMessage("action", name + "(" + AsString(args) + ")");
	};
	agent.WhenObservation = [this](const String& name, const String& obs) {
		AddMessage("observation", obs.Left(500));
	};
	agent.WhenFinished = [this](const String& res) {
		AddMessage("assistant", res);
	};
	agent.WhenError = [this](const String& err) {
		AddMessage("error", err);
	};

	bool has_key = false;
	OpenAiCompatibleProvider real_provider;

	if (!force_mock) {
		AiServiceProvider* def_p = AiManager().GetDefaultProvider();
		if (def_p && !def_p->token.IsEmpty()) {
			real_provider.api_key = def_p->token;
			if (!def_p->url.IsEmpty())
				real_provider.endpoint = def_p->GetChatEndpoint();
			has_key = true;
		}
		else {
			String env_key = GetEnv("OPENROUTER_API_KEY");
			if (!env_key.IsEmpty()) {
				real_provider.api_key = env_key;
				has_key = true;
			}
		}
	}

	if (has_key) {
		agent.SetProvider(&real_provider);
		agent.ExecuteTask(task);
	}
	else {
		MockSweProvider mock_prov(task);
		agent.SetProvider(&mock_prov);
		agent.ExecuteTask(task);
	}
}

void ChatAiCtrl::ClearMessages() {
	if (!ext) return;
	ChatThread& th = GetExt<ChatThread>();
	th.messages.Clear();
	messages.Clear();
	WhenEditorChange();
}

END_UPP_NAMESPACE
