#ifndef _SuperIntelligence_Provider_h_
#define _SuperIntelligence_Provider_h_

#include <Core/Core.h>
#include <Core/SSL/SSL.h>

NAMESPACE_UPP

struct TokenCandidate : Moveable<TokenCandidate> {
	String token;
	double logprob = 0.0;

	TokenCandidate() = default;
	TokenCandidate(const String& t, double lp = 0.0) : token(t), logprob(lp) {}
	TokenCandidate(const TokenCandidate&) = default;
	TokenCandidate(TokenCandidate&&) = default;
	TokenCandidate& operator=(const TokenCandidate&) = default;
	TokenCandidate& operator=(TokenCandidate&&) = default;

	void Jsonize(JsonIO& io) {
		io("token", token)("logprob", logprob);
	}

	template <class VisType>
	void Visit(VisType& v) {
		v("token", token)("logprob", logprob);
	}
};

struct TokenLogprob : Moveable<TokenLogprob> {
	String token;
	double logprob = 0.0;
	Vector<TokenCandidate> top_logprobs;

	TokenLogprob() = default;
	TokenLogprob(const String& t, double lp = 0.0) : token(t), logprob(lp) {}
	TokenLogprob(const TokenLogprob& o, int)
		: token(o.token), logprob(o.logprob), top_logprobs(o.top_logprobs, 1) {}
	TokenLogprob(const TokenLogprob& o)
		: token(o.token), logprob(o.logprob), top_logprobs(clone(o.top_logprobs)) {}
	TokenLogprob(TokenLogprob&& o) = default;
	TokenLogprob& operator=(const TokenLogprob& o) {
		token = o.token;
		logprob = o.logprob;
		top_logprobs = clone(o.top_logprobs);
		return *this;
	}
	TokenLogprob& operator=(TokenLogprob&& o) = default;

	void Jsonize(JsonIO& io) {
		io("token", token)("logprob", logprob)("top_logprobs", top_logprobs);
	}

	template <class VisType>
	void Visit(VisType& v) {
		v("token", token)("logprob", logprob)("top_logprobs", top_logprobs, 0);
	}
};

struct CompletionChoice : Moveable<CompletionChoice> {
	String text;
	int index = 0;
	String finish_reason;
	Vector<TokenLogprob> logprobs;

	CompletionChoice() = default;
	CompletionChoice(const CompletionChoice& o, int)
		: text(o.text), index(o.index), finish_reason(o.finish_reason), logprobs(o.logprobs, 1) {}
	CompletionChoice(const CompletionChoice& o)
		: text(o.text), index(o.index), finish_reason(o.finish_reason), logprobs(clone(o.logprobs)) {}
	CompletionChoice(CompletionChoice&& o) = default;
	CompletionChoice& operator=(const CompletionChoice& o) {
		text = o.text;
		index = o.index;
		finish_reason = o.finish_reason;
		logprobs = clone(o.logprobs);
		return *this;
	}
	CompletionChoice& operator=(CompletionChoice&& o) = default;

	void Jsonize(JsonIO& io) {
		io("text", text)("index", index)("finish_reason", finish_reason)("logprobs", logprobs);
	}
};

struct CompletionUsage : Moveable<CompletionUsage> {
	int prompt_tokens = 0;
	int completion_tokens = 0;
	int total_tokens = 0;

	void Jsonize(JsonIO& io) {
		io("prompt_tokens", prompt_tokens)("completion_tokens", completion_tokens)("total_tokens", total_tokens);
	}
};

struct CompletionResponse : Moveable<CompletionResponse> {
	String id;
	String object;
	int64 created = 0;
	String model;
	Vector<CompletionChoice> choices;
	CompletionUsage usage;

	CompletionResponse() = default;
	CompletionResponse(const CompletionResponse& o, int)
		: id(o.id), object(o.object), created(o.created), model(o.model),
		  choices(o.choices, 1), usage(o.usage) {}
	CompletionResponse(const CompletionResponse& o)
		: id(o.id), object(o.object), created(o.created), model(o.model),
		  choices(clone(o.choices)), usage(o.usage) {}
	CompletionResponse(CompletionResponse&& o) = default;
	CompletionResponse& operator=(const CompletionResponse& o) {
		id = o.id; object = o.object; created = o.created; model = o.model;
		choices = clone(o.choices); usage = o.usage;
		return *this;
	}
	CompletionResponse& operator=(CompletionResponse&& o) = default;

	void Jsonize(JsonIO& io) {
		io("id", id)("object", object)("created", created)("model", model)("choices", choices)("usage", usage);
	}
};

struct CompletionRequest : Moveable<CompletionRequest> {
	String prompt;
	String model;
	int max_tokens = 2048;
	double temperature = 1.0;
	double top_p = 1.0;
	Vector<String> stop;
	int logprobs = 0;
	bool stream = false;

	CompletionRequest() = default;
	CompletionRequest(const CompletionRequest& o, int)
		: prompt(o.prompt), model(o.model), max_tokens(o.max_tokens),
		  temperature(o.temperature), top_p(o.top_p), stop(o.stop, 1),
		  logprobs(o.logprobs), stream(o.stream) {}
	CompletionRequest(const CompletionRequest& o)
		: prompt(o.prompt), model(o.model), max_tokens(o.max_tokens),
		  temperature(o.temperature), top_p(o.top_p), stop(clone(o.stop)),
		  logprobs(o.logprobs), stream(o.stream) {}
	CompletionRequest(CompletionRequest&& o) = default;
	CompletionRequest& operator=(const CompletionRequest& o) {
		prompt = o.prompt; model = o.model; max_tokens = o.max_tokens;
		temperature = o.temperature; top_p = o.top_p; stop = clone(o.stop);
		logprobs = o.logprobs; stream = o.stream;
		return *this;
	}
	CompletionRequest& operator=(CompletionRequest&& o) = default;

	void Jsonize(JsonIO& io) {
		io("prompt", prompt)("model", model)("max_tokens", max_tokens)
		  ("temperature", temperature)("top_p", top_p)("stop", stop)
		  ("logprobs", logprobs)("stream", stream);
	}
};

struct SSEEvent : Moveable<SSEEvent> {
	String event;
	String data;
	bool is_done = false;

	void Jsonize(JsonIO& io) {
		io("event", event)("data", data)("is_done", is_done);
	}
};

struct AiServiceProvider {
	enum Type : int {
		OPENAI = 0,
		OPENAI_COMPLETION,
		OPENAI_CHAT,
		ANTHROPIC,
		GEMINI,
		OLLAMA,
		GENERIC_OPENAI,
		TYPE_COUNT
	};

	Type   type = OPENAI;
	String name;
	String url;
	String token;
	String proxy;
	int    priority = 0;
	int    timeout_secs = 60;

	static String GetTypeString(int t);
	String GetTypeString() const { return GetTypeString(type); }

	static String GetDefaultUrl(int t);
	String GetDefaultUrl() const { return GetDefaultUrl(type); }

	// Endpoint URL normalization
	static String NormalizeBaseUrl(const String& base_url, int t = OPENAI);
	static String NormalizeEndpoint(const String& base_url, const String& endpoint_suffix = "/v1/completions", int t = OPENAI);
	String GetBaseUrl() const { return NormalizeBaseUrl(url, type); }
	String GetCompletionEndpoint() const { return NormalizeEndpoint(url, "/v1/completions", type); }
	String GetChatEndpoint() const { return NormalizeEndpoint(url, "/v1/chat/completions", type); }

	// Legacy /v1/completions JSON builder
	static String BuildCompletionPayload(const CompletionRequest& req);
	static String BuildCompletionPayload(const String& prompt,
	                                     const String& model = "",
	                                     int max_tokens = 2048,
	                                     double temperature = 1.0,
	                                     double top_p = 1.0,
	                                     const Vector<String>& stop = Vector<String>(),
	                                     int logprobs = 0,
	                                     bool stream = false);

	// Legacy /v1/completions JSON parser
	static bool ParseCompletionResponse(const String& json_str, CompletionResponse& resp);
	static bool ParseCompletionResponse(const String& json_str,
	                                    String& text,
	                                    String& finish_reason,
	                                    Vector<TokenLogprob>& logprobs);

	// Logprob parser helper (supports both legacy parallel arrays & structured arrays)
	static void ParseTokenLogprobs(const Value& lp_val, Vector<TokenLogprob>& out);

	// SSE Stream parser helpers
	static bool ParseSSEChunk(const String& chunk, Vector<SSEEvent>& events);
	static bool ParseSSEChunk(const String& chunk,
	                          String& delta_text,
	                          bool& is_done,
	                          Vector<TokenLogprob>* delta_logprobs = nullptr);

	// Direct execution (headless)
	bool ExecuteCompletion(const CompletionRequest& req, CompletionResponse& resp, String* error_msg = nullptr) const;

	void Jsonize(JsonIO& io) {
		io
			("type", (int&)type)
			("name", name)
			("url", url)
			("token", token)
			("proxy", proxy)
			("priority", priority)
			("timeout_secs", timeout_secs);
	}

	void Serialize(Stream& s) {
		s % (int&)type % name % url % token % proxy % priority % timeout_secs;
	}
};

class AiServiceProviderManager {
	Array<AiServiceProvider> providers;
	String config_path;

public:
	typedef AiServiceProviderManager CLASSNAME;
	AiServiceProviderManager();

	AiServiceProvider& Add();
	void Remove(int i);
	AiServiceProvider& operator[](int i) { return providers[i]; }
	const AiServiceProvider& operator[](int i) const { return providers[i]; }
	int GetCount() const { return providers.GetCount(); }

	AiServiceProvider* FindByType(int type);
	AiServiceProvider* GetDefaultProvider();

	void SetConfigPath(const String& path) { config_path = path; }
	bool Load();
	bool Save();

	void Jsonize(JsonIO& io) {
		io("providers", providers);
	}
};

AiServiceProviderManager& AiManager();

END_UPP_NAMESPACE

#endif
