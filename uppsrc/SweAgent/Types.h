#ifndef _SweAgent_Types_h_
#define _SweAgent_Types_h_

enum class MessageRole : int {
	SYSTEM,
	USER,
	ASSISTANT,
	TOOL
};

struct ToolCall : Moveable<ToolCall> {
	String id;
	String name;
	String arguments;

	void Jsonize(JsonIO& json) {
		json("id", id)
			("name", name)
			("arguments", arguments);
	}
	String ToString() const {
		return "ToolCall(" + name + ", id=" + id + ", args=" + arguments + ")";
	}
};

struct Message : Moveable<Message> {
	MessageRole role = MessageRole::USER;
	String content;
	String reasoning;
	String tool_call_id;
	Vector<ToolCall> tool_calls;

	Message() = default;
	Message(const Message& m)
		: role(m.role), content(m.content), reasoning(m.reasoning),
		  tool_call_id(m.tool_call_id), tool_calls(clone(m.tool_calls)) {}
	Message(Message&& m) = default;
	Message& operator=(const Message& m) {
		role = m.role;
		content = m.content;
		reasoning = m.reasoning;
		tool_call_id = m.tool_call_id;
		tool_calls = clone(m.tool_calls);
		return *this;
	}
	Message& operator=(Message&& m) = default;

	void Jsonize(JsonIO& json) {
		int r = (int)role;
		json("role", r)
			("content", content)
			("reasoning", reasoning)
			("tool_call_id", tool_call_id)
			("tool_calls", tool_calls);
		if(json.IsLoading())
			role = (MessageRole)r;
	}
};

struct GenerationUsage : Moveable<GenerationUsage> {
	int prompt_tokens = 0;
	int completion_tokens = 0;
	int total_tokens = 0;
	double cost = 0.0;

	void Jsonize(JsonIO& json) {
		json("prompt_tokens", prompt_tokens)
			("completion_tokens", completion_tokens)
			("total_tokens", total_tokens)
			("cost", cost);
	}
};

struct GenerationMeta : Moveable<GenerationMeta> {
	String id;
	String model;
	String provider;
	GenerationUsage usage;
	int status_code = 200;

	void Jsonize(JsonIO& json) {
		json("id", id)
			("model", model)
			("provider", provider)
			("usage", usage)
			("status_code", status_code);
	}
	String ToString() const {
		return Format("Generation(id=%s, model=%s, provider=%s, tokens=%d+%d=%d, cost=$%f)",
		              id, model, provider, usage.prompt_tokens, usage.completion_tokens,
		              usage.total_tokens, usage.cost);
	}
};

struct AgentConfig : Moveable<AgentConfig> {
	String provider_endpoint = "https://openrouter.ai/api/v1/chat/completions";
	String api_key;
	String model_name;
	Vector<String> fallback_models;
	int max_steps = 20;
	int timeout_secs = 120;
	int thinking_effort = 0;
	String workspace_dir = ".";
	String system_prompt;
	String system_template;
	String instance_template;
	String mode = "confirm";
	int step_limit = 0;
	double cost_limit = 3.0;
	bool confirm_exit = true;
	String output_path;

	AgentConfig() = default;
	AgentConfig(const AgentConfig& o)
		: provider_endpoint(o.provider_endpoint),
		  api_key(o.api_key),
		  model_name(o.model_name),
		  fallback_models(clone(o.fallback_models)),
		  max_steps(o.max_steps),
		  timeout_secs(o.timeout_secs),
		  thinking_effort(o.thinking_effort),
		  workspace_dir(o.workspace_dir),
		  system_prompt(o.system_prompt),
		  system_template(o.system_template),
		  instance_template(o.instance_template),
		  mode(o.mode),
		  step_limit(o.step_limit),
		  cost_limit(o.cost_limit),
		  confirm_exit(o.confirm_exit),
		  output_path(o.output_path) {}
	AgentConfig(AgentConfig&&) = default;
	AgentConfig& operator=(const AgentConfig& o) {
		provider_endpoint = o.provider_endpoint;
		api_key = o.api_key;
		model_name = o.model_name;
		fallback_models = clone(o.fallback_models);
		max_steps = o.max_steps;
		timeout_secs = o.timeout_secs;
		thinking_effort = o.thinking_effort;
		workspace_dir = o.workspace_dir;
		system_prompt = o.system_prompt;
		system_template = o.system_template;
		instance_template = o.instance_template;
		mode = o.mode;
		step_limit = o.step_limit;
		cost_limit = o.cost_limit;
		confirm_exit = o.confirm_exit;
		output_path = o.output_path;
		return *this;
	}
	AgentConfig& operator=(AgentConfig&&) = default;

	void Jsonize(JsonIO& json) {
		json("provider_endpoint", provider_endpoint)
			("api_key", api_key)
			("model_name", model_name)
			("fallback_models", fallback_models)
			("max_steps", max_steps)
			("timeout_secs", timeout_secs)
			("thinking_effort", thinking_effort)
			("workspace_dir", workspace_dir)
			("system_prompt", system_prompt)
			("system_template", system_template)
			("instance_template", instance_template)
			("mode", mode)
			("step_limit", step_limit)
			("cost_limit", cost_limit)
			("confirm_exit", confirm_exit)
			("output_path", output_path);
	}

	void Visit(Vis& vis) {
		vis("provider_endpoint", provider_endpoint)
		   ("api_key", api_key)
		   ("model_name", model_name)
		   ("fallback_models", fallback_models)
		   ("max_steps", max_steps)
		   ("timeout_secs", timeout_secs)
		   ("thinking_effort", thinking_effort)
		   ("workspace_dir", workspace_dir)
		   ("system_prompt", system_prompt)
		   ("system_template", system_template)
		   ("instance_template", instance_template)
		   ("mode", mode)
		   ("step_limit", step_limit)
		   ("cost_limit", cost_limit)
		   ("confirm_exit", confirm_exit)
		   ("output_path", output_path);
	}
};

#endif
