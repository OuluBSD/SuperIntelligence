#include "Provider.h"

NAMESPACE_UPP

String AiServiceProvider::GetTypeString(int t) {
	switch (t) {
	case OPENAI:            return "OpenAI (Default)";
	case OPENAI_COMPLETION: return "OpenAI Completion API";
	case OPENAI_CHAT:       return "OpenAI Chat API";
	case ANTHROPIC:         return "Anthropic Claude";
	case GEMINI:            return "Google Gemini";
	case OLLAMA:            return "Ollama (Local)";
	case GENERIC_OPENAI:    return "Generic OpenAI-Compatible";
	default:                return "Unknown";
	}
}

AiServiceProviderManager::AiServiceProviderManager() {
	config_path = ConfigFile("ai_providers.json");
	Load();
}

AiServiceProvider& AiServiceProviderManager::Add() {
	return providers.Add();
}

void AiServiceProviderManager::Remove(int i) {
	if (i >= 0 && i < providers.GetCount())
		providers.Remove(i);
}

bool AiServiceProviderManager::Load() {
	if (!FileExists(config_path))
		return false;
	String json = LoadFile(config_path);
	if (json.IsEmpty())
		return false;
	return LoadFromJson(*this, json);
}

bool AiServiceProviderManager::Save() {
	String json = StoreAsJson(*this, true);
	return SaveFile(config_path, json);
}

AiServiceProviderManager& AiManager() {
	static AiServiceProviderManager m;
	return m;
}

END_UPP_NAMESPACE
