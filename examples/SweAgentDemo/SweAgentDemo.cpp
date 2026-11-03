#include <SweAgent/SweAgent.h>

using namespace Upp;

static String SelectModelFromAllowed(OpenAiCompatibleProvider& provider)
{
	Vector<String> allowed = provider.GetAllowedModels();
	String env_model = GetEnv("OPENROUTER_MODEL");

	if(!env_model.IsEmpty()) {
		// Verify if env_model or its base name is in allowed list
		for(int i = 0; i < allowed.GetCount(); i++) {
			if(allowed[i] == env_model || allowed[i].Mid(0, allowed[i].GetCount() - 5) == env_model)
				return env_model;
		}
		Cout() << "Note: OPENROUTER_MODEL '" << env_model << "' selected by environment override.\n";
		return env_model;
	}

	if(allowed.IsEmpty()) {
		Cerr() << "Error: No allowed models found in ~/openrouter-models.txt\n";
		return "google/gemma-4-26b-a4b-it:free";
	}

	// Try to find first preferred candidate from allowed list
	for(int i = 0; i < allowed.GetCount(); i++) {
		const String& m = allowed[i];
		if(m.Find("gemma") >= 0 || m.Find("nemotron") >= 0 || m.Find("flint") >= 0)
			return m;
	}
	return allowed[0];
}

static void AttachOpenRouterLogger(Agent& agent)
{
	agent.WhenGeneration = [](const GenerationMeta& meta) {
		Cout() << "--- [OpenRouter Server Generation] ---\n"
		       << "  Generation ID: " << meta.id << "\n"
		       << "  Serving Model: " << meta.model << "\n"
		       << "  Provider:      " << meta.provider << "\n"
		       << "  Prompt Tokens: " << meta.usage.prompt_tokens << "\n"
		       << "  Compl. Tokens: " << meta.usage.completion_tokens << "\n"
		       << "  Total Tokens:  " << meta.usage.total_tokens << "\n"
		       << "  Est. Cost:     $" << Format("%.7f", meta.usage.cost) << "\n"
		       << "-------------------------------------\n";
	};
	agent.WhenReasoning = [](const String& reasoning) {
		Cout() << "[OpenRouter Reasoning/Thinking]\n" << reasoning << "\n\n";
	};
	agent.WhenThought = [](const String& t) {
		Cout() << "[Model Output]\n" << t << "\n";
	};
	agent.WhenAction = [](const String& tool, const ValueMap& args) {
		Cout() << "[Tool Action Call] " << tool << " " << AsJSON(Value(args)) << "\n";
	};
	agent.WhenObservation = [](const String& tool, const String& obs) {
		Cout() << "[Tool Observation Output]\n" << obs << "\n";
	};
	agent.WhenFinished = [](const String& fin) {
		Cout() << "\n=== [Task Completed Successfully] ===\n" << fin << "\n";
	};
	agent.WhenError = [](const String& err) {
		Cerr() << "[Agent Error] " << err << "\n";
	};
}

static bool RunTest8(OpenAiCompatibleProvider& provider, const String& model)
{
	Cout() << "========================================================\n"
	       << "=== Task 8: OpenRouter Chat Completion Test          ===\n"
	       << "========================================================\n";
	Cout() << "Target Model (from ~/openrouter-models.txt): " << model << "\n\n";

	Agent agent(5);
	agent.SetProvider(&provider);
	agent.config.model_name = model;
	AttachOpenRouterLogger(agent);

	String prompt = "Explain in two clear sentences what an autonomous software engineering agent is and how it uses tools.";
	Cout() << "Sending prompt to OpenRouter: \"" << prompt << "\"\n\n";

	bool ok = agent.ExecuteTask(prompt);
	if(ok && !provider.last_generation.id.IsEmpty()) {
		Cout() << "\n>>> Task 8 PASSED (Verified via OpenRouter GenID: " << provider.last_generation.id << ") <<<\n";
		return true;
	}
	Cout() << "\n>>> Task 8 FAILED <<<\n";
	return false;
}

static bool RunTest9(OpenAiCompatibleProvider& provider, const String& model)
{
	Cout() << "========================================================\n"
	       << "=== Task 9: OpenRouter Tool Usage Test (Shell)       ===\n"
	       << "========================================================\n";
	Cout() << "Target Model (from ~/openrouter-models.txt): " << model << "\n\n";

	Agent agent(10);
	agent.SetProvider(&provider);
	agent.config.model_name = model;
	agent.AddTool(new ShellTool);
	AttachOpenRouterLogger(agent);

	String prompt = "Use the shell tool to execute 'uname -s -m' to find out the operating system kernel and hardware architecture, then report what you found.";
	Cout() << "Sending prompt to OpenRouter: \"" << prompt << "\"\n\n";

	bool ok = agent.ExecuteTask(prompt);
	if(ok && !provider.last_generation.id.IsEmpty()) {
		Cout() << "\n>>> Task 9 PASSED (Verified via OpenRouter GenID: " << provider.last_generation.id << ") <<<\n";
		return true;
	}
	Cout() << "\n>>> Task 9 FAILED <<<\n";
	return false;
}

static bool RunTest10(OpenAiCompatibleProvider& provider, const String& model)
{
	Cout() << "========================================================\n"
	       << "=== Task 10: OpenRouter Directory Functions Test     ===\n"
	       << "========================================================\n";
	Cout() << "Target Model (from ~/openrouter-models.txt): " << model << "\n\n";

	Agent agent(10);
	agent.SetProvider(&provider);
	agent.config.model_name = model;
	agent.AddTool(new ListDirTool);
	agent.AddTool(new FileReadTool);
	agent.AddTool(new ShellTool);
	AttachOpenRouterLogger(agent);

	String prompt = "Use the list_dir tool to inspect the contents of uppsrc/SweAgent/, identify what .h header files exist there, and tell me what SweAgent.h contains.";
	Cout() << "Sending prompt to OpenRouter: \"" << prompt << "\"\n\n";

	bool ok = agent.ExecuteTask(prompt);
	if(ok && !provider.last_generation.id.IsEmpty()) {
		Cout() << "\n>>> Task 10 PASSED (Verified via OpenRouter GenID: " << provider.last_generation.id << ") <<<\n";
		return true;
	}
	Cout() << "\n>>> Task 10 FAILED <<<\n";
	return false;
}

static bool RunTest11(OpenAiCompatibleProvider& provider, const String& model)
{
	Cout() << "========================================================\n"
	       << "=== Task 11: OpenRouter File Functions Test (Write/Read)===\n"
	       << "========================================================\n";
	Cout() << "Target Model (from ~/openrouter-models.txt): " << model << "\n\n";

	Agent agent(10);
	agent.SetProvider(&provider);
	agent.config.model_name = model;
	agent.AddTool(new FileWriteTool);
	agent.AddTool(new FileReadTool);
	agent.AddTool(new FileEditTool);
	agent.AddTool(new ShellTool);
	AttachOpenRouterLogger(agent);

	String test_file = "sweagent_live_verify.txt";
	FileDelete(test_file);

	String prompt =
		"Execute these steps carefully using your tools:\n"
		"1. Use file_write to write 'OpenRouter Live SWE-Agent Test' into file 'sweagent_live_verify.txt'.\n"
		"2. Use file_read to read 'sweagent_live_verify.txt' back and verify its content.\n"
		"3. Use shell to delete 'sweagent_live_verify.txt'.\n"
		"Report the outcome once finished.";
	Cout() << "Sending prompt to OpenRouter: \"" << prompt << "\"\n\n";

	bool ok = agent.ExecuteTask(prompt);
	FileDelete(test_file);

	if(ok && !provider.last_generation.id.IsEmpty()) {
		Cout() << "\n>>> Task 11 PASSED (Verified via OpenRouter GenID: " << provider.last_generation.id << ") <<<\n";
		return true;
	}
	Cout() << "\n>>> Task 11 FAILED <<<\n";
	return false;
}

static bool RunThinkingTest(OpenAiCompatibleProvider& provider, const String& model)
{
	Cout() << "========================================================\n"
	       << "=== Thinking Mode Test (OpenRouter Reasoning Feature)===\n"
	       << "========================================================\n";
	Cout() << "Target Model (from ~/openrouter-models.txt): " << model << "\n\n";

	provider.thinking_effort = 2; // medium thinking effort

	Agent agent(5);
	agent.SetProvider(&provider);
	agent.config.model_name = model;
	agent.config.thinking_effort = 2;
	AttachOpenRouterLogger(agent);

	String prompt = "How many times does the letter 'r' appear in 'strawberry'? Think step by step.";
	Cout() << "Sending prompt with OpenRouter thinking enabled: \"" << prompt << "\"\n\n";

	bool ok = agent.ExecuteTask(prompt);
	provider.thinking_effort = 0;

	if(ok && !provider.last_generation.id.IsEmpty()) {
		Cout() << "\n>>> Thinking Mode Test PASSED (Verified via OpenRouter GenID: " << provider.last_generation.id << ") <<<\n";
		return true;
	}
	Cout() << "\n>>> Thinking Mode Test FAILED <<<\n";
	return false;
}

static bool RunTest15()
{
	Cout() << "========================================================\n";
	Cout() << "=== Task 15: OpenRouter Fallbacks & Text Action Test ===\n";
	Cout() << "========================================================\n";

	// 1. Prefix normalization
	String norm1 = OpenAiCompatibleProvider::NormalizeModelSlug("openrouter/nvidia/nemotron-3.5-super-vl");
	if(norm1 != "nvidia/nemotron-3.5-super-vl") {
		Cerr() << "FAILED: NormalizeModelSlug failed for openrouter/ prefix: " << norm1 << "\n";
		return false;
	}
	String norm2 = OpenAiCompatibleProvider::NormalizeModelSlug("google/gemma-4-31b-it");
	if(norm2 != "google/gemma-4-31b-it") {
		Cerr() << "FAILED: NormalizeModelSlug altered bare model: " << norm2 << "\n";
		return false;
	}

	// 2. Reasoning model detection
	if(!OpenAiCompatibleProvider::IsReasoningModel("openai/o1-preview") ||
	   !OpenAiCompatibleProvider::IsReasoningModel("deepseek/deepseek-r1") ||
	   !OpenAiCompatibleProvider::IsReasoningModel("openrouter/openai/o3-mini")) {
		Cerr() << "FAILED: IsReasoningModel failed for known reasoning model\n";
		return false;
	}
	if(OpenAiCompatibleProvider::IsReasoningModel("google/gemma-4-31b-it")) {
		Cerr() << "FAILED: IsReasoningModel false positive on gemma\n";
		return false;
	}

	// 3. Text action extraction from markdown blocks
	String sample_content = "I will check the files now:\n```mswea_bash_command\nls -la\n```\nAnd then inspect git:\n```bash\ngit status\n```\nDone.";
	Vector<String> extracted = OpenAiCompatibleProvider::ExtractTextActions(sample_content);
	if(extracted.GetCount() != 2 || extracted[0] != "ls -la" || extracted[1] != "git status") {
		Cerr() << "FAILED: ExtractTextActions mismatch (count=" << extracted.GetCount() << ")\n";
		return false;
	}

	// 4. ParseResponse text-action fallback verification
	OpenAiCompatibleProvider prov;
	String fake_json = "{\"id\":\"gen-test-123\",\"model\":\"test-model\",\"choices\":[{\"message\":{\"role\":\"assistant\",\"content\":\"Let me run this:\\n```mswea_bash_command\\necho TEST_FALLBACK_OK\\n```\"}}]}";
	Message resp;
	if(!prov.ParseResponse(fake_json, resp)) {
		Cerr() << "FAILED: ParseResponse failed\n";
		return false;
	}
	if(resp.tool_calls.GetCount() != 1 || resp.tool_calls[0].name != "shell") {
		Cerr() << "FAILED: Text-action fallback did not create synthetic tool call\n";
		return false;
	}
	ValueMap args = ParseJSON(resp.tool_calls[0].arguments);
	if(AsString(args["command"]) != "echo TEST_FALLBACK_OK") {
		Cerr() << "FAILED: Synthetic tool call argument mismatch: " << resp.tool_calls[0].arguments << "\n";
		return false;
	}

	Cout() << "Task 15 PASSED (Verified Provider Prefix, Parameter Fallbacks, and Text Action Fallback)\n";
	return true;
}

static bool RunTest16()
{
	Cout() << "========================================================\n";
	Cout() << "=== Task 16: YAML Config & Visit Serialization Test  ===\n";
	Cout() << "========================================================\n";

	// 1. Verify default SweAgentConfig values
	SweAgentConfig cfg;
	if(cfg.agent.mode != "confirm" || cfg.agent.cost_limit != 3.0 ||
	   !cfg.model.drop_params || cfg.environment.environment_class != "local") {
		Cerr() << "FAILED: Default config mismatch (mode=" << cfg.agent.mode
		       << ", cost_limit=" << cfg.agent.cost_limit << ")\n";
		return false;
	}

	// 2. Verify YAML loading & partial override (merging behavior)
	String custom_yaml =
		"agent:\n"
		"  mode: \"yolo\"\n"
		"  cost_limit: 7.25\n"
		"model:\n"
		"  model_name: \"nvidia/nemotron-3.5-super-vl\"\n"
		"run:\n"
		"  task: \"Fix issue in parser\"\n";

	if(!cfg.LoadYAML(custom_yaml)) {
		Cerr() << "FAILED: LoadYAML failed on valid YAML\n";
		return false;
	}

	if(cfg.agent.mode != "yolo" || cfg.agent.cost_limit != 7.25 ||
	   cfg.model.model_name != "nvidia/nemotron-3.5-super-vl" ||
	   cfg.run.task != "Fix issue in parser") {
		Cerr() << "FAILED: Overridden fields mismatch after LoadYAML\n";
		return false;
	}

	// Unmentioned fields should keep defaults
	if(cfg.agent.system_template.IsEmpty() || !cfg.model.drop_params || cfg.environment.environment_class != "local") {
		Cerr() << "FAILED: Unmentioned fields were corrupted by partial YAML load\n";
		return false;
	}

	// 3. Verify YAML serialization (ToYAML)
	String serialized = cfg.ToYAML();
	Cout() << "Serialized YAML:\n" << serialized << "\n";
	if(serialized.IsEmpty() || serialized.Find("yolo") < 0 || serialized.Find("nemotron") < 0) {
		Cerr() << "FAILED: ToYAML output missing expected keys:\n" << serialized << "\n";
		return false;
	}

	// 4. Verify file round-trip (LoadYAMLFile)
	String tmp_file = "/tmp/test_sweagent_cfg_roundtrip.yaml";
	if(!SaveFile(tmp_file, serialized)) {
		Cerr() << "FAILED: Could not write temporary YAML config file to " << tmp_file << "\n";
		return false;
	}

	SweAgentConfig cfg2;
	Value parsed_v = ParseYAML(serialized);
	if(parsed_v.IsError()) {
		Cerr() << "ParseYAML error on serialized string: " << GetErrorText(parsed_v) << "\n";
	}
	if(!cfg2.LoadYAMLFile(tmp_file)) {
		FileDelete(tmp_file);
		Cerr() << "FAILED: LoadYAMLFile failed (content count=" << LoadFile(tmp_file).GetCount() << ")\n";
		return false;
	}
	FileDelete(tmp_file);

	if(cfg2.agent.mode != "yolo" || cfg2.run.task != "Fix issue in parser" ||
	   cfg2.model.model_name != "nvidia/nemotron-3.5-super-vl") {
		Cerr() << "FAILED: Round-tripped config values mismatch\n";
		return false;
	}

	Cout() << "Task 16 PASSED (Verified VisitFromYAML, VisitToYAML, Config Merging & Roundtrip)\n";
	return true;
}

CONSOLE_APP_MAIN
{
	const Vector<String>& cmd = CommandLine();

	OpenAiCompatibleProvider provider;
	if(!provider.LoadApiKey()) {
		Cerr() << "Error: No OpenRouter API key found. Expected in ~/openrouter-apikey.txt or OPENROUTER_API_KEY\n";
		SetExitCode(1);
		return;
	}

	String model = SelectModelFromAllowed(provider);

	if(cmd.GetCount() >= 2 && cmd[0] == "test") {
		int test_id = ScanInt(cmd[1]);
		bool success = false;
		if(test_id == 8)
			success = RunTest8(provider, model);
		else if(test_id == 9)
			success = RunTest9(provider, model);
		else if(test_id == 10)
			success = RunTest10(provider, model);
		else if(test_id == 11)
			success = RunTest11(provider, model);
		else if(test_id == 12)
			success = RunThinkingTest(provider, model);
		else if(test_id == 15)
			success = RunTest15();
		else if(test_id == 16)
			success = RunTest16();
		else {
			Cerr() << "Unknown test id: " << test_id << "\n";
			SetExitCode(1);
			return;
		}
		SetExitCode(success ? 0 : 1);
		return;
	}

	if(cmd.GetCount() >= 1 && (cmd[0] == "all-tests" || cmd[0] == "--test")) {
		bool all_ok = true;
		if(!RunTest8(provider, model)) all_ok = false;
		if(!RunTest9(provider, model)) all_ok = false;
		if(!RunTest10(provider, model)) all_ok = false;
		if(!RunTest11(provider, model)) all_ok = false;
		if(!RunThinkingTest(provider, model)) all_ok = false;

		Cout() << "\n=======================================================\n";
		Cout() << "=== Test Summary: " << (all_ok ? "ALL TESTS PASSED" : "SOME TESTS FAILED") << " ===\n";
		Cout() << "=======================================================\n";
		SetExitCode(all_ok ? 0 : 1);
		return;
	}

	String prompt = "Check what files are in the current directory and print a brief summary.";
	if(cmd.GetCount() > 0)
		prompt = cmd[0];

	Cout() << "Running SweAgent with prompt: " << prompt << "\n";
	Cout() << "Using model: " << model << "\n\n";

	Agent agent(10);
	agent.SetProvider(&provider);
	agent.config.model_name = model;
	agent.AddTool(new ShellTool);
	agent.AddTool(new FileReadTool);
	agent.AddTool(new FileWriteTool);
	agent.AddTool(new FileEditTool);
	agent.AddTool(new ListDirTool);
	AttachOpenRouterLogger(agent);

	bool ok = agent.ExecuteTask(prompt);
	SetExitCode(ok ? 0 : 1);
}
