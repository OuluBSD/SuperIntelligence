#ifndef _SuperIntelligence_Ctrl_CompletionComponent_h_
#define _SuperIntelligence_Ctrl_CompletionComponent_h_

#include <Vfs/Ctrl/Ctrl.h>

NAMESPACE_UPP

struct CompletionThread : VfsValueExt {
	CLASSTYPE(CompletionThread)

	String         model_name = "gpt-4o";
	double         temperature = 0.7;
	int            max_tokens = 2048;
	double         top_p = 1.0;
	String         stop_seq;
	String         prompt;
	String         completion;
	Vector<String> history;

	CompletionThread(VfsValue& n);
	virtual ~CompletionThread();

	void Visit(Vis& v) override;
};

class CompletionCtrl : public WithCompletionLayout<VfsValueExtCtrl> {
public:
	typedef CompletionCtrl CLASSNAME;

	CompletionCtrl();
	virtual ~CompletionCtrl();

	void Data() override;
	void Submit();
	void ClearPrompt();
	void SyncToThread();
};

END_UPP_NAMESPACE

#endif
