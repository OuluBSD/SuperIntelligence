#include "SweAgent.h"

NAMESPACE_UPP

String SweAgentConfig::GetDefaultMiniYaml()
{
	return
		"agent:\n"
		"  system_template: \"You are a helpful assistant that can interact with a computer.\"\n"
		"  instance_template: \"Please solve this issue: {{task}}\"\n"
		"  mode: \"confirm\"\n"
		"  step_limit: 0\n"
		"  cost_limit: 3.0\n"
		"  confirm_exit: true\n"
		"model:\n"
		"  model_name: \"\"\n"
		"  drop_params: true\n"
		"  cost_tracking: \"default\"\n"
		"environment:\n"
		"  environment_class: \"local\"\n"
		"  cwd: \".\"\n"
		"run:\n"
		"  task: \"\"\n";
}

SweAgentConfig::SweAgentConfig()
{
	LoadYAML(GetDefaultMiniYaml());
}

bool SweAgentConfig::LoadYAML(const String& yaml_content)
{
	if(yaml_content.IsEmpty())
		return false;
	return VisitFromYAML(*this, yaml_content);
}

bool SweAgentConfig::LoadYAMLFile(const String& path)
{
	String content = LoadFile(path);
	if(content.IsEmpty())
		return false;
	return LoadYAML(content);
}

String SweAgentConfig::ToYAML() const
{
	return VisitToYAML(const_cast<SweAgentConfig&>(*this));
}

END_UPP_NAMESPACE
