#ifndef _SweAgent_Provider_h_
#define _SweAgent_Provider_h_

class LlmProvider {
public:
	virtual ~LlmProvider() {}

	virtual bool ChatCompletion(const Vector<Message>& messages,
	                            const Vector<Tool*>& tools,
	                            const String& model,
	                            Message& response_msg) = 0;

	virtual bool ChatCompletion(const Vector<Message>& messages,
	                            const String& model,
	                            String& content_result) = 0;

	virtual bool LoadApiKey() = 0;
	virtual Vector<String> GetAllowedModels() = 0;
};

class OpenAiCompatibleProvider : public LlmProvider {
public:
	String endpoint;
	String api_key;
	String api_key_path;
	String models_path;
	Vector<String> allowed_models;
	int timeout_secs;
	int thinking_effort;
	bool drop_params;
	double temperature;
	GenerationMeta last_generation;

	OpenAiCompatibleProvider();
	virtual ~OpenAiCompatibleProvider() {}

	virtual bool ChatCompletion(const Vector<Message>& messages,
	                            const Vector<Tool*>& tools,
	                            const String& model,
	                            Message& response_msg) override;

	virtual bool ChatCompletion(const Vector<Message>& messages,
	                            const String& model,
	                            String& content_result) override;

	virtual bool LoadApiKey() override;
	virtual Vector<String> GetAllowedModels() override;

	static String NormalizeModelSlug(const String& model);
	static Vector<String> ExtractTextActions(const String& content);
	static bool IsReasoningModel(const String& model);

	Vector<String> GetModelFallbackCandidates(const String& requested_model);

	String BuildRequestBody(const Vector<Message>& messages,
	                        const Vector<Tool*>& tools,
	                        const String& model,
	                        const Vector<String>& routing_fallbacks = Vector<String>()) const;

	bool ParseResponse(const String& body, Message& response_msg);
};

#endif
