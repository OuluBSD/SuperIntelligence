#ifndef _SuperIntelligence_Ctrl_PlaygroundWindow_h_
#define _SuperIntelligence_Ctrl_PlaygroundWindow_h_

#include "CompletionComponent.h"
#include "ChatAiComponent.h"
#include "AiSetupCtrl.h"

NAMESPACE_UPP

void RegisterPlaygroundComponents();

class PlaygroundWindow : public VfsTreeAppWindow {
public:
	typedef PlaygroundWindow CLASSNAME;

	PlaygroundWindow();
	virtual ~PlaygroundWindow();

	void MainMenu(Bar& bar) override;
	void AiMenu(Bar& bar);
	void InitDemoWorkspace();
};

void RunAiPlayground();

END_UPP_NAMESPACE

#endif
