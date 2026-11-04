#ifndef _SuperIntelligence_Ctrl_CompletionComponent_h_
#define _SuperIntelligence_Ctrl_CompletionComponent_h_

#include <Vfs/Ctrl/Ctrl.h>
#include <SuperIntelligence/Provider/Provider.h>

NAMESPACE_UPP

struct CompletionThread : VfsValueExt {
	CLASSTYPE(CompletionThread)

	String               model_name = "gpt-4o";
	double               temperature = 0.7;
	int                  max_tokens = 2048;
	double               top_p = 1.0;
	String               stop_seq;
	String               prompt;
	String               completion;
	Vector<String>       history;
	bool                 stream = true;
	bool                 show_logprobs = true;
	int                  top_logprobs = 5;
	Vector<TokenLogprob> tokens;

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

	void UpdateTokenView();
	void OnTokenLink(const String& link);
	void ReplaceToken(int idx, const String& new_token);

	// Streaming helpers
	bool AppendSSEChunk(const String& chunk);
	void AppendStreamDelta(const String& delta_text, const Vector<TokenLogprob>& delta_tokens = Vector<TokenLogprob>());

	// Token formatting helper
	static String FormatTokensQTF(const Vector<TokenLogprob>& tokens);

private:
	void GenerateMockCompletion();
};

END_UPP_NAMESPACE

#endif
