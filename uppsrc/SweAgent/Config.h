#ifndef _SweAgent_Config_h_
#define _SweAgent_Config_h_

struct ModelConfig : Moveable<ModelConfig> {
	String model_name;
	bool drop_params = true;
	double temperature = -1.0;
	String cost_tracking = "default";
	String observation_template;
	String format_error_template;

	void Jsonize(JsonIO& json) {
		json("model_name", model_name)
			("drop_params", drop_params)
			("temperature", temperature)
			("cost_tracking", cost_tracking)
			("observation_template", observation_template)
			("format_error_template", format_error_template);
	}

	void Visit(Vis& vis) {
		vis("model_name", model_name)
		   ("drop_params", drop_params)
		   ("temperature", temperature)
		   ("cost_tracking", cost_tracking)
		   ("observation_template", observation_template)
		   ("format_error_template", format_error_template);
	}
};

struct EnvironmentConfig : Moveable<EnvironmentConfig> {
	String environment_class = "local";
	String cwd = ".";
	VectorMap<String, String> env;

	EnvironmentConfig() = default;
	EnvironmentConfig(const EnvironmentConfig& o)
		: environment_class(o.environment_class), cwd(o.cwd), env(clone(o.env)) {}
	EnvironmentConfig(EnvironmentConfig&&) = default;
	EnvironmentConfig& operator=(const EnvironmentConfig& o) {
		environment_class = o.environment_class;
		cwd = o.cwd;
		env = clone(o.env);
		return *this;
	}
	EnvironmentConfig& operator=(EnvironmentConfig&&) = default;

	void Jsonize(JsonIO& json) {
		json("environment_class", environment_class)
			("cwd", cwd);
	}

	void Visit(Vis& vis) {
		vis("environment_class", environment_class)
		   ("cwd", cwd);
	}
};

struct RunConfig : Moveable<RunConfig> {
	String task;

	void Jsonize(JsonIO& json) {
		json("task", task);
	}

	void Visit(Vis& vis) {
		vis("task", task);
	}
};

struct SweAgentConfig : Moveable<SweAgentConfig> {
	AgentConfig       agent;
	ModelConfig       model;
	EnvironmentConfig environment;
	RunConfig         run;

	SweAgentConfig();
	SweAgentConfig(const SweAgentConfig& o)
		: agent(o.agent), model(o.model), environment(o.environment), run(o.run) {}
	SweAgentConfig(SweAgentConfig&&) = default;
	SweAgentConfig& operator=(const SweAgentConfig& o) {
		agent = o.agent;
		model = o.model;
		environment = o.environment;
		run = o.run;
		return *this;
	}
	SweAgentConfig& operator=(SweAgentConfig&&) = default;

	void Jsonize(JsonIO& json) {
		json("agent", agent)
			("model", model)
			("environment", environment)
			("run", run);
	}

	void Visit(Vis& vis) {
		vis.Visit("agent", agent);
		vis.Visit("model", model);
		vis.Visit("environment", environment);
		vis.Visit("run", run);
	}

	static String GetDefaultMiniYaml();
	bool LoadYAML(const String& yaml_content);
	bool LoadYAMLFile(const String& path);
	String ToYAML() const;
};

#endif
