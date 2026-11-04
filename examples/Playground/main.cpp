#include <SuperIntelligence/Ctrl/Ctrl.h>
#include <math.h>

namespace Upp {
#ifdef UPP_HEAP
extern bool NoMemoryLeaksCheck;
#endif
}

using namespace Upp;

GUI_APP_MAIN {
	PlaygroundWindow win;

	const Vector<String>& cmd = CommandLine();
	if (cmd.GetCount() > 0 && cmd[0] == "--test") {
		Cout() << "Playground: Running automated self-test...\n";

		VfsValue& root = win.app.GetRoot();
		ASSERT(root.id == "Playground");
		ASSERT(root.sub.GetCount() >= 2);
		ASSERT(root.sub[0].id == "Text Completion");
		ASSERT(root.sub[1].id == "General Chat");

		// Test Completion component
		ASSERT(~root.sub[0].ext != nullptr);
		CompletionThread* comp = dynamic_cast<CompletionThread*>(&*root.sub[0].ext);
		ASSERT(comp != nullptr);
		ASSERT(!comp->prompt.IsEmpty());

		// Test Chat component
		ASSERT(~root.sub[1].ext != nullptr);
		ChatThread* chat = dynamic_cast<ChatThread*>(&*root.sub[1].ext);
		ASSERT(chat != nullptr);
		ASSERT(chat->messages.GetCount() >= 2);

		// Test 1: QTF token visualization formatting with probability shading and links
		{
			Cout() << "  Testing QTF token visualization formatting...\n";
			Vector<TokenLogprob> tokens;
			// Green: p >= 0.8
			TokenLogprob& t0 = tokens.Add(TokenLogprob("Hello", log(0.95)));
			t0.top_logprobs.Add(TokenCandidate("Hello", log(0.95)));
			t0.top_logprobs.Add(TokenCandidate("Hi", log(0.04)));

			// Yellow-green: p >= 0.5
			TokenLogprob& t1 = tokens.Add(TokenLogprob(" world", log(0.65)));
			t1.top_logprobs.Add(TokenCandidate(" world", log(0.65)));
			t1.top_logprobs.Add(TokenCandidate(" there", log(0.30)));

			// Orange-yellow: p >= 0.2
			TokenLogprob& t2 = tokens.Add(TokenLogprob(" from", log(0.35)));

			// Red-pink: p < 0.2
			TokenLogprob& t3 = tokens.Add(TokenLogprob(" AI", log(0.10)));

			String qtf = CompletionCtrl::FormatTokensQTF(tokens);
			ASSERT(qtf.Find("(185.245.185)") >= 0); // Green
			ASSERT(qtf.Find("(225.245.185)") >= 0); // Yellow-green
			ASSERT(qtf.Find("(255.230.170)") >= 0); // Orange-yellow
			ASSERT(qtf.Find("(255.195.195)") >= 0); // Red-pink
			ASSERT(qtf.Find("token:0") >= 0);
			ASSERT(qtf.Find("token:1") >= 0);
			ASSERT(qtf.Find("token:2") >= 0);
			ASSERT(qtf.Find("token:3") >= 0);

			// Test rendering in RichTextView
			RichTextView rtv;
			rtv.SetQTF(qtf);
		}

		// Test 2: Token candidate replacement
		{
			Cout() << "  Testing token candidate replacement...\n";
			VfsValue node;
			CompletionThread th(node);
			th.prompt = "The sky is blue";
			th.completion = "The sky is blue";

			TokenLogprob& t0 = th.tokens.Add(TokenLogprob("The", log(0.99)));
			TokenLogprob& t1 = th.tokens.Add(TokenLogprob(" sky", log(0.95)));
			TokenLogprob& t2 = th.tokens.Add(TokenLogprob(" is", log(0.90)));
			TokenLogprob& t3 = th.tokens.Add(TokenLogprob(" blue", log(0.70)));
			t3.top_logprobs.Add(TokenCandidate(" blue", log(0.70)));
			t3.top_logprobs.Add(TokenCandidate(" dark", log(0.25)));
			t3.top_logprobs.Add(TokenCandidate(" grey", log(0.05)));

			CompletionCtrl ctrl;
			ctrl.ext = &th;
			ctrl.Data();

			// Replace token 3 (" blue" -> " dark")
			ctrl.ReplaceToken(3, " dark");
			ASSERT(th.tokens[3].token == " dark");
			ASSERT(th.completion == "The sky is dark");
			ASSERT(th.prompt == "The sky is dark");

			String updated_qtf = CompletionCtrl::FormatTokensQTF(th.tokens);
			ASSERT(updated_qtf.Find(" dark") >= 0);
			ctrl.ext = nullptr;
		}

		// Test 3: SSE stream chunk delta appending
		{
			Cout() << "  Testing SSE stream chunk delta appending...\n";
			VfsValue node;
			CompletionThread th(node);
			th.prompt = "Tell me: ";
			th.completion = "";

			CompletionCtrl ctrl;
			ctrl.ext = &th;
			ctrl.Data();

			// Stream chunk 1
			String chunk1 = "data: {\"choices\":[{\"text\":\"Hello\",\"index\":0,\"logprobs\":{\"tokens\":[\"Hello\"],\"token_logprobs\":[-0.05],\"top_logprobs\":[{\"Hello\":-0.05}]}}]}\n\n";
			bool done1 = ctrl.AppendSSEChunk(chunk1);
			ASSERT(!done1);
			ASSERT(th.completion == "Hello");
			ASSERT(th.tokens.GetCount() == 1);
			ASSERT(th.tokens[0].token == "Hello");

			// Stream chunk 2
			String chunk2 = "data: {\"choices\":[{\"text\":\" AI\",\"index\":0,\"logprobs\":{\"tokens\":[\" AI\"],\"token_logprobs\":[-0.10],\"top_logprobs\":[{\" AI\":-0.10}]}}]}\n\n";
			bool done2 = ctrl.AppendSSEChunk(chunk2);
			ASSERT(!done2);
			ASSERT(th.completion == "Hello AI");
			ASSERT(th.tokens.GetCount() == 2);
			ASSERT(th.tokens[1].token == " AI");

			// Stream chunk 3 ([DONE])
			String chunk3 = "data: [DONE]\n\n";
			bool done3 = ctrl.AppendSSEChunk(chunk3);
			ASSERT(done3 == true);
			ASSERT(th.completion == "Hello AI");
			ASSERT(th.tokens.GetCount() == 2);

			ctrl.ext = nullptr;
		}

		// Test workspace serialization and CompletionThread JSON round-trip
		{
			Cout() << "  Testing workspace and CompletionThread serialization...\n";
			comp->tokens.Add(TokenLogprob("fib", log(0.9)));
			comp->tokens[0].top_logprobs.Add(TokenCandidate("fib", log(0.9)));
			comp->tokens[0].top_logprobs.Add(TokenCandidate("fibonacci", log(0.1)));

			// Test direct CompletionThread serialization (which exercises Visit)
			String comp_json = StoreAsJson(*comp);
			ASSERT(!comp_json.IsEmpty());

			VfsValue test_node;
			CompletionThread loaded_comp(test_node);
			LoadFromJson(loaded_comp, comp_json);
			ASSERT(loaded_comp.tokens.GetCount() == 1);
			ASSERT(loaded_comp.tokens[0].token == "fib");
			ASSERT(loaded_comp.tokens[0].top_logprobs.GetCount() == 2);
			ASSERT(loaded_comp.tokens[0].top_logprobs[1].token == "fibonacci");

			// Test tree save/load
			String temp_file = ConfigFile("test_playground_workspace.json");
			bool save_ok = win.app.SaveTree(temp_file);
			ASSERT(save_ok);

			bool load_ok = win.app.LoadTree(temp_file);
			ASSERT(load_ok);
			ASSERT(win.app.GetRoot().id == "Playground");

			DeleteFile(temp_file);
		}

		// Test 4: ChatThread serialization round-trip with SWE mode and trajectory messages
		{
			Cout() << "  Testing ChatThread serialization with SWE mode...\n";
			VfsValue node;
			ChatThread th(node);
			th.model_name = "claude-3-5-sonnet";
			th.system_instruction = "You are an autonomous SWE agent.";
			th.temperature = 0.5;
			th.swe_mode = true;
			th.workspace_dir = "/tmp/test_workspace";
			th.max_steps = 15;
			th.messages.Add(ChatThread::Message("user", "Inspect repository"));
			th.messages.Add(ChatThread::Message("thought", "Listing files"));
			th.messages.Add(ChatThread::Message("action", "list_dir({})"));
			th.messages.Add(ChatThread::Message("observation", "main.cpp"));
			th.messages.Add(ChatThread::Message("assistant", "Found main.cpp"));

			String json = StoreAsJson(th);
			ASSERT(!json.IsEmpty());
			ASSERT(json.Find("swe_mode") >= 0);
			ASSERT(json.Find("workspace_dir") >= 0);
			ASSERT(json.Find("max_steps") >= 0);

			VfsValue loaded_node;
			ChatThread loaded_th(loaded_node);
			LoadFromJson(loaded_th, json);

			ASSERT(loaded_th.model_name == "claude-3-5-sonnet");
			ASSERT(loaded_th.system_instruction == "You are an autonomous SWE agent.");
			ASSERT(loaded_th.temperature == 0.5);
			ASSERT(loaded_th.swe_mode == true);
			ASSERT(loaded_th.workspace_dir == "/tmp/test_workspace");
			ASSERT(loaded_th.max_steps == 15);
			ASSERT(loaded_th.messages.GetCount() == 5);
			ASSERT(loaded_th.messages[0].role == "user" && loaded_th.messages[0].content == "Inspect repository");
			ASSERT(loaded_th.messages[1].role == "thought" && loaded_th.messages[1].content == "Listing files");
			ASSERT(loaded_th.messages[2].role == "action" && loaded_th.messages[2].content == "list_dir({})");
			ASSERT(loaded_th.messages[3].role == "observation" && loaded_th.messages[3].content == "main.cpp");
			ASSERT(loaded_th.messages[4].role == "assistant" && loaded_th.messages[4].content == "Found main.cpp");
		}

		// Test 5: ChatAiCtrl in SWE Agent mode executing a task with event callbacks
		{
			Cout() << "  Testing ChatAiCtrl SWE Agent task execution and event callbacks...\n";
			VfsValue node;
			ChatThread th(node);
			th.swe_mode = true;
			th.workspace_dir = ".";
			th.max_steps = 5;

			ChatAiCtrl ctrl;
			ctrl.force_mock = true;
			ctrl.ext = &th;
			ctrl.Data();

			// Execute task
			ctrl.ExecuteSweTask("Inspect repository contents");

			// Verify messages populated in ChatThread
			ASSERT(th.messages.GetCount() >= 5);

			bool has_user = false;
			bool has_thought = false;
			bool has_action = false;
			bool has_observation = false;
			bool has_assistant = false;

			for (int i = 0; i < th.messages.GetCount(); i++) {
				const auto& m = th.messages[i];
				if (m.role == "user") has_user = true;
				if (m.role == "thought") has_thought = true;
				if (m.role == "action") has_action = true;
				if (m.role == "observation") has_observation = true;
				if (m.role == "assistant") has_assistant = true;
			}

			ASSERT(has_user);
			ASSERT(has_thought);
			ASSERT(has_action);
			ASSERT(has_observation);
			ASSERT(has_assistant);

			// Verify UI ArrayCtrl rows match ChatThread messages
			ASSERT(ctrl.messages.GetCount() == th.messages.GetCount());

			// Also test SendMessage() with SWE mode enabled
			int count_before = th.messages.GetCount();
			ctrl.input.SetData("Check git status");
			ctrl.SendMessage();
			ASSERT(th.messages.GetCount() > count_before);
			ASSERT(ctrl.messages.GetCount() == th.messages.GetCount());

			ctrl.ext = nullptr;
		}

		Cout() << "Playground: Automated self-test passed successfully!\n";
		win.app.ClearExtensionCtrl();
		win.app.GetRoot().sub.Clear();
#ifdef UPP_HEAP
		NoMemoryLeaksCheck = true;
#endif
		return;
	}

	win.Run();
}
