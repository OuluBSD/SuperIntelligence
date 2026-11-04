#include "Ctrl.h"

NAMESPACE_UPP

ChatThread::ChatThread(VfsValue& n) : VfsValueExt(n) {
}

ChatThread::~ChatThread() {
}

void ChatThread::Visit(Vis& v) {
	v
		("model_name", model_name)
		("system_instruction", system_instruction)
		("temperature", temperature);
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

	messages.AddColumn("Role");
	messages.AddColumn("Message");
	messages.ColumnWidths("70 300");

	model_name.WhenAction = THISBACK(SyncToThread);
	system_instruction.WhenAction = THISBACK(SyncToThread);
	temperature.WhenAction = THISBACK(SyncToThread);

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

	WhenEditorChange();
}

void ChatAiCtrl::SendMessage() {
	SyncToThread();
	if (!ext) return;
	ChatThread& th = GetExt<ChatThread>();

	String user_text = TrimBoth((String)input.GetData());
	if (user_text.IsEmpty()) return;

	th.messages.Add(ChatThread::Message("user", user_text));
	messages.Add("user", user_text);

	// Generate assistant response
	String ai_reply = "Assistant reply to: " + user_text;
	th.messages.Add(ChatThread::Message("assistant", ai_reply));
	messages.Add("assistant", ai_reply);

	input.SetData("");
	if (messages.GetCount() > 0)
		messages.SetCursor(messages.GetCount() - 1);

	WhenEditorChange();
}

void ChatAiCtrl::ClearMessages() {
	if (!ext) return;
	ChatThread& th = GetExt<ChatThread>();
	th.messages.Clear();
	messages.Clear();
	WhenEditorChange();
}

END_UPP_NAMESPACE
