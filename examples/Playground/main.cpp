#include <SuperIntelligence/Ctrl/Ctrl.h>

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

		// Test workspace serialization
		String temp_file = ConfigFile("test_playground_workspace.json");
		bool save_ok = win.app.SaveTree(temp_file);
		ASSERT(save_ok);

		bool load_ok = win.app.LoadTree(temp_file);
		ASSERT(load_ok);
		ASSERT(win.app.GetRoot().id == "Playground");

		DeleteFile(temp_file);
		Cout() << "Playground: Automated self-test passed successfully!\n";
		return;
	}

	win.Run();
}
