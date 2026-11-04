#ifndef _SuperIntelligence_Provider_h_
#define _SuperIntelligence_Provider_h_

#include <Core/Core.h>

NAMESPACE_UPP

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

	static String GetTypeString(int t);
	String GetTypeString() const { return GetTypeString(type); }

	void Jsonize(JsonIO& io) {
		io
			("type", (int&)type)
			("name", name)
			("url", url)
			("token", token)
			("proxy", proxy)
			("priority", priority);
	}

	void Serialize(Stream& s) {
		s % (int&)type % name % url % token % proxy % priority;
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
