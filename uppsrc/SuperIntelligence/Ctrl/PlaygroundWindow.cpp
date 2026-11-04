#include "Ctrl.h"

NAMESPACE_UPP

void RegisterPlaygroundComponents() {
	static bool registered = false;
	if (registered) return;
	registered = true;

	VfsValueExtFactory::Register<CompletionThread>("Completion", VFSEXT_COMPONENT, "completion", "AI Models");
	VfsValueExtFactory::RegisterCtrl<CompletionThread, CompletionCtrl>("CompletionCtrl");

	VfsValueExtFactory::Register<ChatThread>("ChatAI", VFSEXT_COMPONENT, "chat_ai", "AI Models");
	VfsValueExtFactory::RegisterCtrl<ChatThread, ChatAiCtrl>("ChatAiCtrl");
}

PlaygroundWindow::PlaygroundWindow() {
	RegisterPlaygroundComponents();

	Title("AI Playground - SuperIntelligence");
	SetRect(0, 0, 1024, 700);

	InitDemoWorkspace();
}

PlaygroundWindow::~PlaygroundWindow() {
}

void PlaygroundWindow::MainMenu(Bar& bar) {
	bar.Sub("File", THISBACK(FileMenu));
	bar.Sub("Edit", THISBACK(EditMenu));
	bar.Sub("AI", THISBACK(AiMenu));
	bar.Sub("View", THISBACK(ViewMenu));
	bar.Sub("Help", THISBACK(HelpMenu));
}

void PlaygroundWindow::AiMenu(Bar& bar) {
	bar.Add("AI Accounts & Providers...", [this] {
		RunAiSetupModal();
	});
}

void PlaygroundWindow::InitDemoWorkspace() {
	VfsValue& root = app.GetRoot();
	root.id = "Playground";

	hash_t comp_th = TypedStringHasher<CompletionThread>("Completion");
	hash_t chat_th = TypedStringHasher<ChatThread>("ChatAI");

	app.AddNode(&root, "Text Completion", comp_th);
	if (root.sub.GetCount() > 0 && root.sub[0].ext) {
		CompletionThread* th = dynamic_cast<CompletionThread*>(&*root.sub[0].ext);
		if (th) {
			th->prompt = "Write a quick C++ function to compute fibonacci numbers:";
		}
	}

	app.AddNode(&root, "General Chat", chat_th);
	if (root.sub.GetCount() > 1 && root.sub[1].ext) {
		ChatThread* th = dynamic_cast<ChatThread*>(&*root.sub[1].ext);
		if (th) {
			th->messages.Add(ChatThread::Message("user", "Hello AI!"));
			th->messages.Add(ChatThread::Message("assistant", "Hello! How can I assist you today?"));
		}
	}

	app.SyncTree();
}

END_UPP_NAMESPACE
