#ifndef _SuperIntelligence_Ctrl_ChatAiComponent_h_
#define _SuperIntelligence_Ctrl_ChatAiComponent_h_

#include <Vfs/Ctrl/Ctrl.h>

NAMESPACE_UPP

struct ChatThread : VfsValueExt {
	CLASSTYPE(ChatThread)

	struct Message : Moveable<Message> {
		String role;
		String content;
		Time   timestamp;

		Message() { timestamp = GetSysTime(); }
		Message(const String& r, const String& c) : role(r), content(c) { timestamp = GetSysTime(); }

		void Visit(Vis& v) {
			v
				("role", role)
				("content", content);
		}
	};

	String          model_name = "gpt-4o";
	String          system_instruction = "You are an intelligent AI assistant.";
	double          temperature = 0.7;
	Vector<Message> messages;

	ChatThread(VfsValue& n);
	virtual ~ChatThread();

	void Visit(Vis& v) override;
};

class ChatAiCtrl : public WithChatAiLayout<VfsValueExtCtrl> {
public:
	typedef ChatAiCtrl CLASSNAME;

	ChatAiCtrl();
	virtual ~ChatAiCtrl();

	void Data() override;
	void SendMessage();
	void ClearMessages();
	void SyncToThread();
};

END_UPP_NAMESPACE

#endif
