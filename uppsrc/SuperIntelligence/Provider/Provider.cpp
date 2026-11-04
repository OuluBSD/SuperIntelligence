#include "Provider.h"

NAMESPACE_UPP

String AiServiceProvider::GetTypeString(int t) {
	switch (t) {
	case OPENAI:            return "OpenAI (Default)";
	case OPENAI_COMPLETION: return "OpenAI Completion API";
	case OPENAI_CHAT:       return "OpenAI Chat API";
	case ANTHROPIC:         return "Anthropic Claude";
	case GEMINI:            return "Google Gemini";
	case OLLAMA:            return "Ollama (Local)";
	case GENERIC_OPENAI:    return "Generic OpenAI-Compatible";
	default:                return "Unknown";
	}
}

String AiServiceProvider::GetDefaultUrl(int t) {
	switch (t) {
	case OPENAI:
	case OPENAI_COMPLETION:
	case OPENAI_CHAT:       return "https://api.openai.com/v1";
	case ANTHROPIC:         return "https://api.anthropic.com/v1";
	case GEMINI:            return "https://generativelanguage.googleapis.com/v1beta";
	case OLLAMA:            return "http://localhost:11434/v1";
	case GENERIC_OPENAI:    return "http://localhost:8000/v1";
	default:                return "http://localhost:8000/v1";
	}
}

String AiServiceProvider::NormalizeBaseUrl(const String& base_url, int t) {
	String u = TrimBoth(base_url);
	if (u.IsEmpty())
		return GetDefaultUrl(t);

	if (!u.StartsWith("http://") && !u.StartsWith("https://"))
		u = "http://" + u;

	while (u.EndsWith("/"))
		u.Remove(u.GetCount() - 1, 1);

	// Strip endpoint subpaths if user supplied a full endpoint
	if (u.EndsWith("/chat/completions"))
		u.Remove(u.GetCount() - 17, 17);
	else if (u.EndsWith("/completions"))
		u.Remove(u.GetCount() - 12, 12);

	while (u.EndsWith("/"))
		u.Remove(u.GetCount() - 1, 1);

	return u;
}

String AiServiceProvider::NormalizeEndpoint(const String& base_url, const String& endpoint_suffix, int t) {
	String base = NormalizeBaseUrl(base_url, t);
	String suffix = TrimBoth(endpoint_suffix);
	if (!suffix.StartsWith("/"))
		suffix = "/" + suffix;

	if (base.EndsWith("/v1") && suffix.StartsWith("/v1/")) {
		suffix = suffix.Mid(3);
	}
	else if (!base.EndsWith("/v1") && !suffix.StartsWith("/v1/")) {
		suffix = "/v1" + suffix;
	}

	return base + suffix;
}

String AiServiceProvider::BuildCompletionPayload(const CompletionRequest& req) {
	ValueMap root;
	root.Add("prompt", req.prompt);
	if (!req.model.IsEmpty())
		root.Add("model", req.model);
	if (req.max_tokens > 0)
		root.Add("max_tokens", req.max_tokens);
	if (req.temperature >= 0.0)
		root.Add("temperature", req.temperature);
	if (req.top_p >= 0.0)
		root.Add("top_p", req.top_p);
	if (!req.stop.IsEmpty()) {
		ValueArray arr;
		for (int i = 0; i < req.stop.GetCount(); i++)
			arr.Add(req.stop[i]);
		root.Add("stop", arr);
	}
	if (req.logprobs > 0)
		root.Add("logprobs", req.logprobs);
	if (req.stream)
		root.Add("stream", true);

	return AsJSON(Value(root));
}

String AiServiceProvider::BuildCompletionPayload(const String& prompt,
                                                 const String& model,
                                                 int max_tokens,
                                                 double temperature,
                                                 double top_p,
                                                 const Vector<String>& stop,
                                                 int logprobs,
                                                 bool stream) {
	CompletionRequest req;
	req.prompt = prompt;
	req.model = model;
	req.max_tokens = max_tokens;
	req.temperature = temperature;
	req.top_p = top_p;
	req.stop <<= stop;
	req.logprobs = logprobs;
	req.stream = stream;
	return BuildCompletionPayload(req);
}

void AiServiceProvider::ParseTokenLogprobs(const Value& lp_val, Vector<TokenLogprob>& out) {
	if (lp_val.Is<ValueMap>()) {
		ValueMap lp_map = lp_val;
		// Legacy /v1/completions format: tokens, token_logprobs, top_logprobs
		if (lp_map["tokens"].Is<ValueArray>()) {
			ValueArray tokens = lp_map["tokens"];
			ValueArray token_lps = lp_map["token_logprobs"];
			ValueArray top_lps = lp_map["top_logprobs"];
			int count = tokens.GetCount();
			for (int i = 0; i < count; i++) {
				TokenLogprob& tlp = out.Add();
				tlp.token = tokens[i].ToString();
				if (i < token_lps.GetCount() && !token_lps[i].IsNull())
					tlp.logprob = (double)token_lps[i];
				if (i < top_lps.GetCount() && !top_lps[i].IsNull()) {
					Value top_elem = top_lps[i];
					if (top_elem.Is<ValueMap>()) {
						ValueMap top_vm = top_elem;
						for (int j = 0; j < top_vm.GetCount(); j++) {
							TokenCandidate& cand = tlp.top_logprobs.Add();
							cand.token = top_vm.GetKey(j).ToString();
							cand.logprob = (double)top_vm.GetValue(j);
						}
					}
				}
			}
			return;
		}
		// Content array format (chat completions / alternate)
		if (lp_map["content"].Is<ValueArray>()) {
			ValueArray content_arr = lp_map["content"];
			for (int i = 0; i < content_arr.GetCount(); i++) {
				Value item = content_arr[i];
				if (!item.Is<ValueMap>()) continue;
				ValueMap vm = item;
				TokenLogprob& tlp = out.Add();
				tlp.token = vm["token"].ToString();
				tlp.logprob = (double)vm["logprob"];
				Value top = vm["top_logprobs"];
				if (top.Is<ValueArray>()) {
					ValueArray top_arr = top;
					for (int j = 0; j < top_arr.GetCount(); j++) {
						Value cand_val = top_arr[j];
						if (cand_val.Is<ValueMap>()) {
							ValueMap cand_vm = cand_val;
							TokenCandidate& cand = tlp.top_logprobs.Add();
							cand.token = cand_vm["token"].ToString();
							cand.logprob = (double)cand_vm["logprob"];
						}
					}
				}
			}
			return;
		}
	}
	else if (lp_val.Is<ValueArray>()) {
		ValueArray arr = lp_val;
		for (int i = 0; i < arr.GetCount(); i++) {
			Value item = arr[i];
			if (!item.Is<ValueMap>()) continue;
			ValueMap vm = item;
			TokenLogprob& tlp = out.Add();
			tlp.token = vm["token"].ToString();
			tlp.logprob = (double)vm["logprob"];
			Value top = vm["top_logprobs"];
			if (top.Is<ValueArray>()) {
				ValueArray top_arr = top;
				for (int j = 0; j < top_arr.GetCount(); j++) {
					Value cand_val = top_arr[j];
					if (cand_val.Is<ValueMap>()) {
						ValueMap cand_vm = cand_val;
						TokenCandidate& cand = tlp.top_logprobs.Add();
						cand.token = cand_vm["token"].ToString();
						cand.logprob = (double)cand_vm["logprob"];
					}
				}
			}
		}
	}
}

bool AiServiceProvider::ParseCompletionResponse(const String& json_str, CompletionResponse& resp) {
	Value val = ParseJSON(json_str);
	if (val.IsError() || !val.Is<ValueMap>())
		return false;

	ValueMap vm = val;
	resp.id = vm["id"].ToString();
	resp.object = vm["object"].ToString();
	resp.model = vm["model"].ToString();
	if (!vm["created"].IsNull())
		resp.created = (int64)vm["created"];

	Value u = vm["usage"];
	if (u.Is<ValueMap>()) {
		ValueMap um = u;
		resp.usage.prompt_tokens = um["prompt_tokens"];
		resp.usage.completion_tokens = um["completion_tokens"];
		resp.usage.total_tokens = um["total_tokens"];
	}

	Value chs = vm["choices"];
	if (chs.Is<ValueArray>()) {
		ValueArray arr = chs;
		for (int i = 0; i < arr.GetCount(); i++) {
			Value item = arr[i];
			if (!item.Is<ValueMap>()) continue;
			ValueMap cm = item;
			CompletionChoice& choice = resp.choices.Add();
			choice.index = cm["index"];
			if (!cm["finish_reason"].IsNull())
				choice.finish_reason = cm["finish_reason"].ToString();
			if (cm["text"].Is<String>())
				choice.text = cm["text"].ToString();
			else if (cm["message"].Is<ValueMap>() && cm["message"]["content"].Is<String>())
				choice.text = cm["message"]["content"].ToString();

			if (!cm["logprobs"].IsNull()) {
				ParseTokenLogprobs(cm["logprobs"], choice.logprobs);
			}
		}
	}

	return !resp.choices.IsEmpty();
}

bool AiServiceProvider::ParseCompletionResponse(const String& json_str,
                                                String& text,
                                                String& finish_reason,
                                                Vector<TokenLogprob>& logprobs) {
	CompletionResponse resp;
	if (!ParseCompletionResponse(json_str, resp))
		return false;

	if (resp.choices.IsEmpty())
		return false;

	const CompletionChoice& ch0 = resp.choices[0];
	text = ch0.text;
	finish_reason = ch0.finish_reason;
	logprobs <<= ch0.logprobs;
	return true;
}

bool AiServiceProvider::ParseSSEChunk(const String& chunk, Vector<SSEEvent>& events) {
	if (chunk.IsEmpty())
		return false;

	String current_event = "message";
	int pos = 0;
	bool found = false;

	while (pos < chunk.GetCount()) {
		int next = chunk.Find('\n', pos);
		String line;
		if (next >= 0) {
			line = chunk.Mid(pos, next - pos);
			pos = next + 1;
		}
		else {
			line = chunk.Mid(pos);
			pos = chunk.GetCount();
		}
		if (line.EndsWith("\r"))
			line.Remove(line.GetCount() - 1, 1);

		line = TrimBoth(line);
		if (line.IsEmpty())
			continue;

		if (line.StartsWith("event:")) {
			current_event = TrimBoth(line.Mid(6));
		}
		else if (line.StartsWith("data:")) {
			String data = TrimBoth(line.Mid(5));
			SSEEvent& ev = events.Add();
			ev.event = current_event;
			ev.data = data;
			ev.is_done = (data == "[DONE]");
			found = true;
		}
	}
	return found;
}

bool AiServiceProvider::ParseSSEChunk(const String& chunk,
                                      String& delta_text,
                                      bool& is_done,
                                      Vector<TokenLogprob>* delta_logprobs) {
	Vector<SSEEvent> events;
	if (!ParseSSEChunk(chunk, events))
		return false;

	for (int i = 0; i < events.GetCount(); i++) {
		const SSEEvent& ev = events[i];
		if (ev.is_done) {
			is_done = true;
			continue;
		}

		Value v = ParseJSON(ev.data);
		if (v.IsError() || !v.Is<ValueMap>())
			continue;

		ValueMap vm = v;
		Value choices = vm["choices"];
		if (!choices.Is<ValueArray>() || choices.GetCount() == 0)
			continue;

		Value ch0 = choices[0];
		if (!ch0.Is<ValueMap>())
			continue;

		ValueMap cm0 = ch0;
		if (cm0["text"].Is<String>()) {
			delta_text.Cat(cm0["text"].ToString());
		}
		else if (cm0["delta"].Is<ValueMap>()) {
			ValueMap dm = cm0["delta"];
			if (dm["content"].Is<String>())
				delta_text.Cat(dm["content"].ToString());
		}

		if (delta_logprobs && !cm0["logprobs"].IsNull()) {
			ParseTokenLogprobs(cm0["logprobs"], *delta_logprobs);
		}
	}
	return true;
}

bool AiServiceProvider::ExecuteCompletion(const CompletionRequest& req,
                                          CompletionResponse& resp,
                                          String* error_msg) const {
	String endpoint = GetCompletionEndpoint();
	String body = BuildCompletionPayload(req);

	HttpRequest http(endpoint);
	http.POST();
	http.ContentType("application/json");
	if (!token.IsEmpty())
		http.Header("Authorization", "Bearer " + token);
	if (!proxy.IsEmpty())
		http.Proxy(proxy);
	http.PostData(body);
	http.Timeout(timeout_secs * 1000);

	String raw_resp = http.Execute();
	if (!http.IsSuccess()) {
		if (error_msg)
			*error_msg = Format("HTTP request failed (%d): %s", http.GetStatusCode(), http.GetErrorDesc());
		return false;
	}

	if (!ParseCompletionResponse(raw_resp, resp)) {
		if (error_msg)
			*error_msg = "Failed to parse completion JSON response";
		return false;
	}

	return true;
}

AiServiceProviderManager::AiServiceProviderManager() {
	config_path = ConfigFile("ai_providers.json");
	Load();
}

AiServiceProvider& AiServiceProviderManager::Add() {
	return providers.Add();
}

void AiServiceProviderManager::Remove(int i) {
	if (i >= 0 && i < providers.GetCount())
		providers.Remove(i);
}

AiServiceProvider* AiServiceProviderManager::FindByType(int t) {
	for (int i = 0; i < providers.GetCount(); i++)
		if (providers[i].type == t)
			return &providers[i];
	return nullptr;
}

AiServiceProvider* AiServiceProviderManager::GetDefaultProvider() {
	if (providers.IsEmpty())
		return nullptr;
	int best_idx = 0;
	int best_prio = providers[0].priority;
	for (int i = 1; i < providers.GetCount(); i++) {
		if (providers[i].priority > best_prio) {
			best_prio = providers[i].priority;
			best_idx = i;
		}
	}
	return &providers[best_idx];
}

bool AiServiceProviderManager::Load() {
	if (!FileExists(config_path))
		return false;
	String json = LoadFile(config_path);
	if (json.IsEmpty())
		return false;
	return LoadFromJson(*this, json);
}

bool AiServiceProviderManager::Save() {
	String json = StoreAsJson(*this, true);
	return SaveFile(config_path, json);
}

AiServiceProviderManager& AiManager() {
	static AiServiceProviderManager m;
	return m;
}

END_UPP_NAMESPACE
