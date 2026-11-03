#include "SweAgent.h"

NAMESPACE_UPP

OpenAiCompatibleProvider::OpenAiCompatibleProvider()
	: endpoint("https://openrouter.ai/api/v1/chat/completions"),
	  api_key_path(GetHomeDirFile("openrouter-apikey.txt")),
	  models_path(GetHomeDirFile("openrouter-models.txt")),
	  timeout_secs(120),
	  thinking_effort(0),
	  drop_params(true),
	  temperature(-1.0)
{
}

String OpenAiCompatibleProvider::NormalizeModelSlug(const String& model)
{
	String m = TrimBoth(model);
	if(m.StartsWith("openrouter/"))
		return m.Mid(11);
	return m;
}

bool OpenAiCompatibleProvider::IsReasoningModel(const String& model)
{
	String m = ToLower(NormalizeModelSlug(model));
	if(m.Find("o1") >= 0 || m.Find("o3") >= 0 || m.Find("gpt-5") >= 0 ||
	   m.Find("r1") >= 0 || m.Find("thinking") >= 0 || m.Find("reasoner") >= 0)
		return true;
	return false;
}

Vector<String> OpenAiCompatibleProvider::ExtractTextActions(const String& content)
{
	Vector<String> actions;
	const char* tags[] = { "```mswea_bash_command", "```bash", "```sh", NULL };

	int pos = 0;
	while(pos < content.GetCount()) {
		int best_start = -1;
		int tag_len = 0;
		for(int t = 0; tags[t]; t++) {
			int f = content.Find(tags[t], pos);
			if(f >= 0 && (best_start < 0 || f < best_start)) {
				best_start = f;
				tag_len = (int)strlen(tags[t]);
			}
		}
		if(best_start < 0)
			break;

		int body_start = best_start + tag_len;
		if(body_start < content.GetCount() && content[body_start] == '\r') body_start++;
		if(body_start < content.GetCount() && content[body_start] == '\n') body_start++;

		int end_fence = content.Find("```", body_start);
		if(end_fence < 0)
			break;

		String cmd = TrimBoth(content.Mid(body_start, end_fence - body_start));
		if(!cmd.IsEmpty())
			actions.Add(cmd);
		pos = end_fence + 3;
	}
	return actions;
}

bool OpenAiCompatibleProvider::LoadApiKey()
{
	if(!api_key.IsEmpty())
		return true;

	String key = LoadFile(api_key_path);
	key = TrimBoth(key);
	if(key.IsEmpty()) {
		LOG("OpenAiCompatibleProvider: Failed to read API key from " << api_key_path);
		return false;
	}
	api_key = key;
	return true;
}

Vector<String> OpenAiCompatibleProvider::GetAllowedModels()
{
	if(!allowed_models.IsEmpty())
		return clone(allowed_models);

	String content = LoadFile(models_path);
	int pos = 0;
	while(pos < content.GetCount()) {
		int next = content.Find(',', pos);
		String token = (next < 0) ? content.Mid(pos) : content.Mid(pos, next - pos);
		token = TrimBoth(token);
		if(!token.IsEmpty())
			allowed_models.Add(NormalizeModelSlug(token));
		if(next < 0)
			break;
		pos = next + 1;
	}
	return clone(allowed_models);
}

Vector<String> OpenAiCompatibleProvider::GetModelFallbackCandidates(const String& requested_model)
{
	Vector<String> candidates;
	GetAllowedModels();

	auto AddCandidate = [&](const String& raw_m) {
		String m = NormalizeModelSlug(raw_m);
		if(m.IsEmpty()) return;
		for(int i = 0; i < candidates.GetCount(); i++)
			if(candidates[i] == m) return;
		candidates.Add(m);
	};

	if(!requested_model.IsEmpty()) {
		AddCandidate(requested_model);
		String norm = NormalizeModelSlug(requested_model);
		if(norm.EndsWith(":free"))
			AddCandidate(norm.Mid(0, norm.GetCount() - 5));
	}

	for(int i = 0; i < allowed_models.GetCount(); i++) {
		const String& m = allowed_models[i];
		AddCandidate(m);
		if(m.EndsWith(":free"))
			AddCandidate(m.Mid(0, m.GetCount() - 5));
	}

	return candidates;
}

String OpenAiCompatibleProvider::BuildRequestBody(const Vector<Message>& messages,
                                                  const Vector<Tool*>& tools,
                                                  const String& model,
                                                  const Vector<String>& routing_fallbacks) const
{
	ValueMap root;
	String norm_model = NormalizeModelSlug(model);
	root.Add("model", norm_model);

	// Add OpenRouter models routing fallback array (up to 3 items total per OpenRouter API spec)
	if(!routing_fallbacks.IsEmpty()) {
		ValueArray models_arr;
		models_arr.Add(norm_model);
		for(int i = 0; i < routing_fallbacks.GetCount() && models_arr.GetCount() < 3; i++) {
			String fb = NormalizeModelSlug(routing_fallbacks[i]);
			if(fb != norm_model) {
				bool exists = false;
				for(int j = 0; j < models_arr.GetCount(); j++) {
					if(models_arr[j] == fb) { exists = true; break; }
				}
				if(!exists)
					models_arr.Add(fb);
			}
		}
		if(models_arr.GetCount() > 1)
			root.Add("models", models_arr);
	}

	// Temperature parameter handling with drop_params fallback
	if(temperature >= 0.0) {
		if(!(drop_params && IsReasoningModel(norm_model))) {
			root.Add("temperature", temperature);
		}
	}

	if(thinking_effort > 0) {
		root.Add("include_reasoning", true);
		ValueMap r;
		if(thinking_effort == 1)
			r.Add("effort", "low");
		else if(thinking_effort == 2)
			r.Add("effort", "medium");
		else
			r.Add("effort", "high");
		root.Add("reasoning", r);
	}

	ValueArray msgs;
	for(int i = 0; i < messages.GetCount(); i++) {
		const Message& m = messages[i];
		ValueMap jm;
		switch(m.role) {
			case MessageRole::SYSTEM:    jm.Add("role", "system"); break;
			case MessageRole::USER:      jm.Add("role", "user"); break;
			case MessageRole::ASSISTANT: jm.Add("role", "assistant"); break;
			case MessageRole::TOOL:      jm.Add("role", "tool"); break;
		}

		if(!m.content.IsEmpty() || m.tool_calls.IsEmpty())
			jm.Add("content", m.content);

		if(!m.tool_call_id.IsEmpty())
			jm.Add("tool_call_id", m.tool_call_id);

		if(!m.tool_calls.IsEmpty()) {
			ValueArray tc_arr;
			for(int j = 0; j < m.tool_calls.GetCount(); j++) {
				const ToolCall& tc = m.tool_calls[j];
				ValueMap fn;
				fn.Add("name", tc.name);
				fn.Add("arguments", tc.arguments);

				ValueMap tc_obj;
				tc_obj.Add("id", tc.id);
				tc_obj.Add("type", "function");
				tc_obj.Add("function", fn);
				tc_arr.Add(tc_obj);
			}
			jm.Add("tool_calls", tc_arr);
		}
		msgs.Add(jm);
	}
	root.Add("messages", msgs);

	if(!tools.IsEmpty()) {
		ValueArray tool_arr;
		for(int i = 0; i < tools.GetCount(); i++) {
			const Tool* t = tools[i];
			ValueMap fn;
			fn.Add("name", t->GetName());
			fn.Add("description", t->GetDescription());
			fn.Add("parameters", t->GetParametersSchema());

			ValueMap to;
			to.Add("type", "function");
			to.Add("function", fn);
			tool_arr.Add(to);
		}
		root.Add("tools", tool_arr);
	}

	return AsJSON(Value(root));
}

bool OpenAiCompatibleProvider::ParseResponse(const String& body, Message& response_msg)
{
	Value root = ParseJSON(body);
	if(root.IsError()) {
		Cerr() << "OpenAiCompatibleProvider: JSON parse error:\n" << body << "\n";
		LOG("OpenAiCompatibleProvider: JSON parse error:\n" << body);
		return false;
	}

	if(!IsNull(root["error"])) {
		Cerr() << "OpenAiCompatibleProvider: API returned error:\n" << AsJSON(root["error"]) << "\n";
		LOG("OpenAiCompatibleProvider: API returned error:\n" << AsJSON(root["error"]));
		return false;
	}

	last_generation.id = AsString(root["id"]);
	last_generation.model = AsString(root["model"]);
	last_generation.provider = AsString(root["provider"]);
	if(!IsNull(root["usage"])) {
		last_generation.usage.prompt_tokens = root["usage"]["prompt_tokens"];
		last_generation.usage.completion_tokens = root["usage"]["completion_tokens"];
		last_generation.usage.total_tokens = root["usage"]["total_tokens"];
		if(!IsNull(root["usage"]["total_cost"]))
			last_generation.usage.cost = root["usage"]["total_cost"];
		else if(!IsNull(root["usage"]["cost"]))
			last_generation.usage.cost = root["usage"]["cost"];
	}

	Value choices = root["choices"];
	if(!IsValueArray(choices) || choices.GetCount() == 0) {
		LOG("OpenAiCompatibleProvider: choices array missing or empty:\n" << body);
		return false;
	}

	Value choice = choices[0];
	Value msg = choice["message"];
	if(!IsValueMap(msg)) {
		LOG("OpenAiCompatibleProvider: message object missing in choice:\n" << body);
		return false;
	}

	response_msg.role = MessageRole::ASSISTANT;
	Value content_val = msg["content"];
	response_msg.content = IsString(content_val) ? String(content_val) : String();

	if(!IsNull(msg["reasoning"]))
		response_msg.reasoning = AsString(msg["reasoning"]);

	Value tool_calls = msg["tool_calls"];
	if(IsValueArray(tool_calls)) {
		for(int i = 0; i < tool_calls.GetCount(); i++) {
			Value tc = tool_calls[i];
			ToolCall call;
			call.id = AsString(tc["id"]);
			Value fn = tc["function"];
			if(IsValueMap(fn)) {
				call.name = AsString(fn["name"]);
				call.arguments = AsString(fn["arguments"]);
			}
			response_msg.tool_calls.Add(call);
		}
	}

	// Text-based action fallback if tool_calls is empty
	if(response_msg.tool_calls.IsEmpty() && !response_msg.content.IsEmpty()) {
		Vector<String> text_actions = ExtractTextActions(response_msg.content);
		for(int i = 0; i < text_actions.GetCount(); i++) {
			ToolCall call;
			call.id = "call_text_" + AsString(i + 1);
			call.name = "shell";
			ValueMap args;
			args.Add("command", text_actions[i]);
			call.arguments = AsJSON(Value(args));
			response_msg.tool_calls.Add(call);
		}
	}

	return true;
}

bool OpenAiCompatibleProvider::ChatCompletion(const Vector<Message>& messages,
                                              const Vector<Tool*>& tools,
                                              const String& model,
                                              Message& response_msg)
{
	if(api_key.IsEmpty()) {
		if(!LoadApiKey())
			return false;
	}

	String target_url = endpoint;
	if(!target_url.EndsWith("/chat/completions")) {
		if(target_url.EndsWith("/"))
			target_url = target_url + "chat/completions";
		else
			target_url = target_url + "/chat/completions";
	}

	String norm_model = NormalizeModelSlug(model);
	Vector<String> candidates = GetModelFallbackCandidates(norm_model);
	if(candidates.IsEmpty())
		candidates.Add(norm_model);

	for(int attempt = 0; attempt < candidates.GetCount(); attempt++) {
		String current_model = candidates[attempt];
		Vector<String> routing_fallbacks;
		for(int r = attempt + 1; r < candidates.GetCount() && routing_fallbacks.GetCount() < 2; r++)
			routing_fallbacks.Add(candidates[r]);

		String body = BuildRequestBody(messages, tools, current_model, routing_fallbacks);

		HttpRequest req(target_url);
		req.POST();
		req.ContentType("application/json");
		req.Header("Authorization", "Bearer " + api_key);
		req.Header("HTTP-Referer", "https://github.com/SeppoPakonen/ai-upp-priv");
		req.Header("X-Title", "SweAgent");
		req.PostData(body);
		req.Timeout(timeout_secs * 1000);

		String resp = req.Execute();
		last_generation.status_code = req.GetStatusCode();
		if(req.IsSuccess()) {
			if(ParseResponse(resp, response_msg))
				return true;
		}
		else {
			// Log fallback attempt to next model candidate from ~/openrouter-models.txt
			if(attempt + 1 < candidates.GetCount()) {
				Cout() << "[OpenRouter Fallback] Model '" << current_model << "' returned HTTP "
				       << req.GetStatusCode() << ". Trying next candidate from ~/openrouter-models.txt: '"
				       << candidates[attempt + 1] << "'...\n";
			}
		}
	}

	return false;
}

bool OpenAiCompatibleProvider::ChatCompletion(const Vector<Message>& messages,
                                              const String& model,
                                              String& content_result)
{
	Vector<Tool*> empty_tools;
	Message resp;
	if(!ChatCompletion(messages, empty_tools, model, resp))
		return false;
	content_result = resp.content;
	return true;
}

END_UPP_NAMESPACE
