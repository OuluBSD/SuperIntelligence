#ifndef _SuperIntelligence_Ctrl_ChatAiComponent_h_
#define _SuperIntelligence_Ctrl_ChatAiComponent_h_

#include <Vfs/Ctrl/Ctrl.h>
#include <SuperIntelligence/Provider/Provider.h>
#include <SweAgent/SweAgent.h>

NAMESPACE_UPP

struct ChatThread : VfsValueExt {
	CLASSTYPE(ChatThread)

	struct Message : Moveable<Message> {
		String role;
		String content;
		Time   timestamp;

		Message() { timestamp = GetSysTime(); }
		Message(const String& r, const String& c) : role(r), content(c) { timestamp = GetSysTime(); }

		void Jsonize(JsonIO& io) {
			io("role", role)("content", content);
		}

		template <class VisType>
		void Visit(VisType& v) {
			v
				("role", role)
				("content", content);
		}
	};

	String          model_name = "gpt-4o";
	String          system_instruction = "You are an intelligent AI assistant.";
	double          temperature = 0.7;
	bool            swe_mode = true;
	String          workspace_dir = ".";
	int             max_steps = 10;
	Vector<Message> messages;

	ChatThread(VfsValue& n);
	virtual ~ChatThread();

	void Visit(Vis& v) override;
};

class ChatAiCtrl : public WithChatAiLayout<VfsValueExtCtrl> {
public:
	typedef ChatAiCtrl CLASSNAME;

	bool force_mock = false;

	ChatAiCtrl();
	virtual ~ChatAiCtrl();

	void Data() override;
	void SendMessage();
	void ClearMessages();
	void SyncToThread();

	void ExecuteSweTask(const String& task);
	void AddMessage(const String& role, const String& content);
};

END_UPP_NAMESPACE

#endif
