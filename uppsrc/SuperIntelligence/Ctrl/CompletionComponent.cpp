#include "Ctrl.h"

NAMESPACE_UPP

CompletionThread::CompletionThread(VfsValue& n) : VfsValueExt(n) {
}

CompletionThread::~CompletionThread() {
}

void CompletionThread::Visit(Vis& v) {
	v
		("model_name", model_name)
		("temperature", temperature)
		("max_tokens", max_tokens)
		("top_p", top_p)
		("stop_seq", stop_seq)
		("prompt", prompt)
		("completion", completion);
}

CompletionCtrl::CompletionCtrl() {
	CtrlLayout(*this);

	model_name.Add("gpt-4o", "OpenAI: GPT-4o");
	model_name.Add("gpt-4-turbo", "OpenAI: GPT-4 Turbo");
	model_name.Add("claude-3-5-sonnet", "Anthropic: Claude 3.5 Sonnet");
	model_name.Add("gemini-1.5-pro", "Google: Gemini 1.5 Pro");
	model_name.Add("llama3", "Ollama: LLaMA 3");

	temperature.MinMax(0.0, 2.0);
	temperature.SetInc(0.05);
	max_tokens.MinMax(1, 32768);
	top_p.MinMax(0.0, 1.0);
	top_p.SetInc(0.05);

	model_name.WhenAction = THISBACK(SyncToThread);
	temperature.WhenAction = THISBACK(SyncToThread);
	max_tokens.WhenAction = THISBACK(SyncToThread);
	top_p.WhenAction = THISBACK(SyncToThread);
	stop_seq.WhenAction = THISBACK(SyncToThread);
	prompt.WhenAction = THISBACK(SyncToThread);

	submit.WhenAction = THISBACK(Submit);
	clear.WhenAction = THISBACK(ClearPrompt);
}

CompletionCtrl::~CompletionCtrl() {
}

void CompletionCtrl::Data() {
	if (!ext) return;
	CompletionThread& th = GetExt<CompletionThread>();

	model_name.SetData(th.model_name);
	temperature.SetData(th.temperature);
	max_tokens.SetData(th.max_tokens);
	top_p.SetData(th.top_p);
	stop_seq.SetData(th.stop_seq);
	prompt.SetData(th.prompt);
}

void CompletionCtrl::SyncToThread() {
	if (!ext) return;
	CompletionThread& th = GetExt<CompletionThread>();

	th.model_name = model_name.GetData();
	th.temperature = temperature.GetData();
	th.max_tokens = max_tokens.GetData();
	th.top_p = top_p.GetData();
	th.stop_seq = stop_seq.GetData();
	th.prompt = prompt.GetData();

	WhenEditorChange();
}

void CompletionCtrl::Submit() {
	SyncToThread();
	if (!ext) return;
	CompletionThread& th = GetExt<CompletionThread>();

	String p = th.prompt;
	if (p.IsEmpty()) return;

	String resp = "\n[AI Completion response for: \"" + p.Left(30) + "...\"]\n";
	th.prompt << resp;
	th.completion = resp;
	th.history.Add(p);

	prompt.SetData(th.prompt);
	WhenEditorChange();
}

void CompletionCtrl::ClearPrompt() {
	if (!ext) return;
	CompletionThread& th = GetExt<CompletionThread>();
	th.prompt.Clear();
	th.completion.Clear();
	prompt.SetData("");
	WhenEditorChange();
}

END_UPP_NAMESPACE
