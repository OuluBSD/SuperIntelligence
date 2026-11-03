#include "SweAgent.h"

NAMESPACE_UPP

Agent::Agent(int max_steps_)
	: max_steps(max_steps_), step(0)
{
	config.max_steps = max_steps_;
}

void Agent::AddTool(Tool* tool)
{
	if(tool) {
		owned_tools.Add(tool);
		tools.Add(tool);
	}
}

void Agent::SetProvider(LlmProvider* prov)
{
	provider = prov;
}

Tool* Agent::FindTool(const String& name)
{
	for(int i = 0; i < tools.GetCount(); i++) {
		if(tools[i]) {
			if(tools[i]->GetName() == name)
				return tools[i];
			Vector<String> aliases = tools[i]->GetAliases();
			for(int j = 0; j < aliases.GetCount(); j++) {
				if(aliases[j] == name)
					return tools[i];
			}
		}
	}
	return nullptr;
}

ValueMap Agent::ParseArgs(const String& raw_args)
{
	ValueMap map;
	if(raw_args.IsEmpty())
		return map;

	Value v = ParseJSON(raw_args);
	if(IsValueMap(v))
		return ValueMap(v);

	int eq = raw_args.Find('=');
	if(eq > 0) {
		String key = TrimBoth(raw_args.Left(eq));
		String val = TrimBoth(raw_args.Mid(eq + 1));
		map.Add(key, val);
	}
	else {
		map.Add("command", raw_args);
	}
	return map;
}

bool Agent::ExecuteTask(const String& instructions)
{
	One<OpenAiCompatibleProvider> fallback_prov;
	if(!provider) {
		fallback_prov.Create();
		if(!config.provider_endpoint.IsEmpty())
			fallback_prov->endpoint = config.provider_endpoint;
		if(!config.api_key.IsEmpty())
			fallback_prov->api_key = config.api_key;
		else
			fallback_prov->LoadApiKey();
		fallback_prov->thinking_effort = config.thinking_effort;
		provider = ~fallback_prov;
	}

	step = 0;
	history.Clear();

	if(!config.system_prompt.IsEmpty()) {
		Message sys_msg;
		sys_msg.role = MessageRole::SYSTEM;
		sys_msg.content = config.system_prompt;
		history.Add(sys_msg);
	}

	Message user_msg;
	user_msg.role = MessageRole::USER;
	user_msg.content = instructions;
	history.Add(user_msg);

	String model = config.model_name;
	String env_model = GetEnv("OPENROUTER_MODEL");
	if(!env_model.IsEmpty())
		model = env_model;

	while(step < max_steps) {
		step++;
		Message response;
		if(!provider->ChatCompletion(history, tools, model, response)) {
			LOG("Agent: provider->ChatCompletion failed at step " << step);
			if(WhenError)
				WhenError("LLM ChatCompletion request failed");
			return false;
		}

		OpenAiCompatibleProvider* op = dynamic_cast<OpenAiCompatibleProvider*>(provider);
		if(op && WhenGeneration)
			WhenGeneration(op->last_generation);

		history.Add(response);

		if(!response.reasoning.IsEmpty()) {
			if(WhenReasoning)
				WhenReasoning(response.reasoning);
		}

		if(!response.content.IsEmpty()) {
			if(WhenThought)
				WhenThought(response.content);
		}

		if(response.tool_calls.IsEmpty()) {
			if(WhenFinished)
				WhenFinished(response.content);
			return true;
		}

		for(int i = 0; i < response.tool_calls.GetCount(); i++) {
			const ToolCall& tc = response.tool_calls[i];
			Tool* tool = FindTool(tc.name);
			ValueMap args = ParseArgs(tc.arguments);

			if(WhenAction)
				WhenAction(tc.name, args);

			String observation;
			if(!tool) {
				observation = "Error: Tool '" + tc.name + "' not found.";
			}
			else {
				Value res = tool->Execute(args, config.workspace_dir);
				observation = AsString(res);
			}

			if(WhenObservation)
				WhenObservation(tc.name, observation);

			Message tool_msg;
			tool_msg.role = MessageRole::TOOL;
			tool_msg.tool_call_id = tc.id;
			tool_msg.content = observation;
			history.Add(tool_msg);
		}
	}

	if(WhenError)
		WhenError("Max steps reached without task completion");
	return false;
}

END_UPP_NAMESPACE
