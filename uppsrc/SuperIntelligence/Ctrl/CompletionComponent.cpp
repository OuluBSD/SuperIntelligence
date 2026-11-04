#include "Ctrl.h"
#include <math.h>

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
		("completion", completion)
		("stream", stream)
		("show_logprobs", show_logprobs)
		("top_logprobs", top_logprobs)
		("tokens", tokens, 0);
}

CompletionCtrl::CompletionCtrl() {
	CtrlLayout(*this);

	view_tabs.Add(prompt.SizePos(), t_("Prompt Editor"));
	view_tabs.Add(token_view.SizePos(), t_("Token & Logprob Inspector"));

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
	top_logprobs.MinMax(1, 20);

	model_name.WhenAction = THISBACK(SyncToThread);
	temperature.WhenAction = THISBACK(SyncToThread);
	max_tokens.WhenAction = THISBACK(SyncToThread);
	top_p.WhenAction = THISBACK(SyncToThread);
	stop_seq.WhenAction = THISBACK(SyncToThread);
	stream.WhenAction = THISBACK(SyncToThread);
	show_logprobs.WhenAction = THISBACK(SyncToThread);
	top_logprobs.WhenAction = THISBACK(SyncToThread);
	prompt.WhenAction = THISBACK(SyncToThread);

	token_view.WhenLink = THISBACK(OnTokenLink);

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
	stream.SetData(th.stream);
	show_logprobs.SetData(th.show_logprobs);
	top_logprobs.SetData(th.top_logprobs);
	prompt.SetData(th.prompt);

	UpdateTokenView();
}

void CompletionCtrl::SyncToThread() {
	if (!ext) return;
	CompletionThread& th = GetExt<CompletionThread>();

	th.model_name = model_name.GetData();
	th.temperature = temperature.GetData();
	th.max_tokens = max_tokens.GetData();
	th.top_p = top_p.GetData();
	th.stop_seq = stop_seq.GetData();
	th.stream = stream.GetData();
	th.show_logprobs = show_logprobs.GetData();
	th.top_logprobs = top_logprobs.GetData();
	th.prompt = prompt.GetData();

	WhenEditorChange();
}

void CompletionCtrl::Submit() {
	SyncToThread();
	if (!ext) return;
	CompletionThread& th = GetExt<CompletionThread>();

	String p = th.prompt;
	if (p.IsEmpty()) return;

	AiServiceProvider* prov = AiManager().GetDefaultProvider();
	bool executed = false;
	if (prov) {
		CompletionRequest req;
		req.prompt = p;
		req.model = th.model_name;
		req.temperature = th.temperature;
		req.max_tokens = th.max_tokens;
		req.top_p = th.top_p;
		if (!th.stop_seq.IsEmpty())
			req.stop.Add(th.stop_seq);
		req.stream = false;
		if (th.show_logprobs)
			req.logprobs = th.top_logprobs > 0 ? th.top_logprobs : 5;

		CompletionResponse resp;
		String err;
		if (prov->ExecuteCompletion(req, resp, &err) && !resp.choices.IsEmpty()) {
			executed = true;
			const auto& choice = resp.choices[0];
			th.completion = choice.text;
			th.tokens = clone(choice.logprobs);
			th.prompt << choice.text;
			th.history.Add(p);
		}
	}

	if (!executed) {
		GenerateMockCompletion();
	}

	prompt.SetData(th.prompt);
	UpdateTokenView();
	if (th.show_logprobs && !th.tokens.IsEmpty()) {
		view_tabs.Set(1);
	}
	WhenEditorChange();
}

void CompletionCtrl::ClearPrompt() {
	if (!ext) return;
	CompletionThread& th = GetExt<CompletionThread>();
	th.prompt.Clear();
	th.completion.Clear();
	th.tokens.Clear();
	prompt.SetData("");
	UpdateTokenView();
	WhenEditorChange();
}

void CompletionCtrl::UpdateTokenView() {
	if (!ext) return;
	CompletionThread& th = GetExt<CompletionThread>();
	token_view.SetQTF(FormatTokensQTF(th.tokens));
}

String CompletionCtrl::FormatTokensQTF(const Vector<TokenLogprob>& tokens) {
	if (tokens.IsEmpty())
		return String("[s0 [/ " + DeQtf(t_("No tokens available. Click Submit to generate.")) + "]]");

	String qtf;
	qtf << "[s0 ";
	for (int i = 0; i < tokens.GetCount(); i++) {
		const TokenLogprob& tok = tokens[i];
		double p = exp(tok.logprob);
		String bg;
		if (p >= 0.8)
			bg = "(185.245.185)";
		else if (p >= 0.5)
			bg = "(225.245.185)";
		else if (p >= 0.2)
			bg = "(255.230.170)";
		else
			bg = "(255.195.195)";

		qtf << "[$" << bg << "@0 [^token:" << i << "^ " << DeQtf(tok.token) << "]]";
	}
	qtf << "]";
	return qtf;
}

void CompletionCtrl::OnTokenLink(const String& link) {
	if (!link.StartsWith("token:"))
		return;
	int idx = ScanInt(link.Mid(6));
	if (!ext)
		return;
	CompletionThread& th = GetExt<CompletionThread>();
	if (idx < 0 || idx >= th.tokens.GetCount())
		return;

	MenuBar::Execute([this, idx](Bar& bar) {
		if (!ext) return;
		CompletionThread& th = GetExt<CompletionThread>();
		if (idx < 0 || idx >= th.tokens.GetCount()) return;

		const auto& cands = th.tokens[idx].top_logprobs;
		if (cands.IsEmpty()) {
			double prob = exp(th.tokens[idx].logprob) * 100.0;
			bar.Add(Format("%s  (%.1f%%)", th.tokens[idx].token, prob), Event<>()).Enable(false);
			bar.Add(t_("No alternative candidates"), Event<>()).Enable(false);
			return;
		}
		for (int i = 0; i < cands.GetCount(); i++) {
			const TokenCandidate& cand = cands[i];
			double prob = exp(cand.logprob) * 100.0;
			String label = Format("%s  (%.1f%%)", cand.token, prob);
			String rep = cand.token;
			bar.Add(label, [this, idx, rep] { ReplaceToken(idx, rep); });
		}
	}, GetMousePos());
}

void CompletionCtrl::ReplaceToken(int idx, const String& new_token) {
	if (!ext) return;
	CompletionThread& th = GetExt<CompletionThread>();
	if (idx < 0 || idx >= th.tokens.GetCount()) return;

	String old_token = th.tokens[idx].token;
	th.tokens[idx].token = new_token;

	// Rebuild completion
	String old_comp = th.completion;
	String new_comp;
	for (const auto& t : th.tokens)
		new_comp << t.token;
	th.completion = new_comp;

	// Update prompt text if it ended with previous completion
	if (!old_comp.IsEmpty() && th.prompt.EndsWith(old_comp)) {
		th.prompt = th.prompt.Left(th.prompt.GetCount() - old_comp.GetCount()) + new_comp;
		prompt.SetData(th.prompt);
	}

	UpdateTokenView();
	WhenEditorChange();
}

bool CompletionCtrl::AppendSSEChunk(const String& chunk) {
	if (!ext) return false;

	String delta_text;
	bool is_done = false;
	Vector<TokenLogprob> delta_logprobs;

	AiServiceProvider::ParseSSEChunk(chunk, delta_text, is_done, &delta_logprobs);

	if (!delta_text.IsEmpty() || !delta_logprobs.IsEmpty()) {
		AppendStreamDelta(delta_text, delta_logprobs);
	}
	return is_done;
}

void CompletionCtrl::AppendStreamDelta(const String& delta_text, const Vector<TokenLogprob>& delta_tokens) {
	if (!ext) return;
	CompletionThread& th = GetExt<CompletionThread>();

	if (!delta_text.IsEmpty()) {
		th.completion << delta_text;
		th.prompt << delta_text;
		prompt.SetData(th.prompt);
	}

	if (!delta_tokens.IsEmpty()) {
		for (int i = 0; i < delta_tokens.GetCount(); i++)
			th.tokens.Add(delta_tokens[i]);
	}
	else if (!delta_text.IsEmpty() && th.show_logprobs) {
		TokenLogprob& tl = th.tokens.Add();
		tl.token = delta_text;
		tl.logprob = 0.0;
		TokenCandidate& tc = tl.top_logprobs.Add();
		tc.token = delta_text;
		tc.logprob = 0.0;
	}

	UpdateTokenView();
	WhenEditorChange();
}

void CompletionCtrl::GenerateMockCompletion() {
	if (!ext) return;
	CompletionThread& th = GetExt<CompletionThread>();

	String p = th.prompt;
	String resp_text = " is an advanced AI system designed to solve complex challenges.";
	th.completion = resp_text;
	th.prompt << resp_text;
	th.history.Add(p);

	th.tokens.Clear();
	struct MockTok { const char* tok; double p; const char* alt1; double a1; const char* alt2; double a2; };
	static const MockTok sample[] = {
		{ " is",         0.92, " was",        0.05, " represents", 0.02 },
		{ " an",         0.85, " a",          0.12, " the",        0.02 },
		{ " advanced",   0.62, " powerful",   0.25, " intelligent",0.10 },
		{ " AI",         0.95, " autonomous", 0.03, " neural",     0.01 },
		{ " system",     0.78, " platform",   0.15, " architecture",0.05 },
		{ " designed",   0.55, " built",      0.30, " created",    0.12 },
		{ " to",         0.88, " for",        0.08, " aimed",      0.02 },
		{ " solve",      0.40, " address",    0.35, " tackle",     0.20 },
		{ " complex",    0.35, " difficult",  0.30, " hard",       0.25 },
		{ " challenges", 0.18, " problems",   0.50, " issues",     0.28 },
		{ ".",           0.99, "!",           0.005,"...",         0.003 }
	};

	for (const auto& m : sample) {
		TokenLogprob& tl = th.tokens.Add();
		tl.token = m.tok;
		tl.logprob = log(max(0.001, m.p));

		TokenCandidate& c0 = tl.top_logprobs.Add();
		c0.token = m.tok;
		c0.logprob = tl.logprob;

		TokenCandidate& c1 = tl.top_logprobs.Add();
		c1.token = m.alt1;
		c1.logprob = log(max(0.001, m.a1));

		TokenCandidate& c2 = tl.top_logprobs.Add();
		c2.token = m.alt2;
		c2.logprob = log(max(0.001, m.a2));
	}
}

END_UPP_NAMESPACE
