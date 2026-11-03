#ifndef _SweAgent_Agent_h_
#define _SweAgent_Agent_h_

class Agent {
public:
	AgentConfig    config;
	LlmProvider*   provider = nullptr;
	Array<Tool>    owned_tools;
	Vector<Tool*>  tools;
	Vector<Message> history;
	int            max_steps = 10;
	int            step = 0;

	// Event callbacks for ReAct loop
	Event<const String&>                  WhenReasoning;
	Event<const String&>                  WhenThought;
	Event<const String&, const ValueMap&> WhenAction;
	Event<const String&, const String&>   WhenObservation;
	Event<const String&>                  WhenFinished;
	Event<const String&>                  WhenError;
	Event<const GenerationMeta&>          WhenGeneration;

	Agent(int max_steps_ = 10);
	~Agent() {}

	void AddTool(Tool* tool);
	void SetProvider(LlmProvider* prov);

	bool ExecuteTask(const String& instructions);
	const Vector<Message>& GetMessages() const { return history; }
	void ClearMessages() { history.Clear(); }

private:
	Tool* FindTool(const String& name);
	ValueMap ParseArgs(const String& raw_args);
};

#endif
