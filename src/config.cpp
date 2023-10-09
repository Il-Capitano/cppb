#include "config.h"
#include "process.h"
#include <filesystem>
#include <fstream>
#include <span>
#include <rapidjson/document.h>
#include <rapidjson/error/error.h>
#include <rapidjson/writer.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/ostreamwrapper.h>
#include <rapidjson/istreamwrapper.h>

static cppb::vector<std::string> get_library_cflags(std::string_view library, config const &config)
{
	auto const cflags = [&]() {
		if (library.substr(0, 4) == "llvm" || library.substr(0, 4) == "LLVM")
		{
			auto const llvm_config = [&]() -> std::string {
				if (!config.llvm_config_path.empty())
				{
					return config.llvm_config_path;
				}
				else if (library.size() == 4)
				{
					return "llvm-config";
				}
				else
				{
					return "llvm_config" + std::string(library.substr(4));
				}
			}();
			auto [flags, is_good] = capture_command_output(llvm_config, {{ "--cflags" }});
			return is_good ? std::move(flags) : "";
		}
		else
		{
			auto [flags, is_good] = capture_command_output("pkg-config", {{ "--cflags", std::string(library) }});
			return is_good ? std::move(flags) : "";
		}
	}();
	std::string_view remaining = cflags;
	cppb::vector<std::string> result;
	while (remaining.size() != 0)
	{
		auto const next_whitespace_pos = remaining.find_first_of(" \n\t");
		auto const flag = remaining.substr(0, next_whitespace_pos);
		if (flag != "")
		{
			result.emplace_back(flag);
		}
		if (next_whitespace_pos == std::string_view::npos)
		{
			break;
		}
		remaining = remaining.substr(next_whitespace_pos + 1);
	}
	return result;
}

static cppb::vector<std::string> get_library_cxxflags(std::string_view library, config const &config)
{
	auto const cxxflags = [&]() {
		if (library.substr(0, 4) == "llvm" || library.substr(0, 4) == "LLVM")
		{
			auto const llvm_config = [&]() -> std::string {
				if (!config.llvm_config_path.empty())
				{
					return config.llvm_config_path;
				}
				else if (library.size() == 4)
				{
					return "llvm-config";
				}
				else
				{
					return "llvm-config" + std::string(library.substr(4));
				}
			}();
			auto [flags, is_good] = capture_command_output(llvm_config, {{ "--cxxflags" }});
			return is_good ? std::move(flags) : "";
		}
		else
		{
			auto [flags, is_good] = capture_command_output("pkg-config", {{ "--cflags", std::string(library) }});
			return is_good ? std::move(flags) : "";
		}
	}();
	std::string_view remaining = cxxflags;
	cppb::vector<std::string> result;
	while (remaining.size() != 0)
	{
		auto const next_whitespace_pos = remaining.find_first_of(" \n\t");
		auto const flag = remaining.substr(0, next_whitespace_pos);
		if (flag != "")
		{
			result.emplace_back(flag);
		}
		if (next_whitespace_pos == std::string_view::npos)
		{
			break;
		}
		remaining = remaining.substr(next_whitespace_pos + 1);
	}
	return result;
}

static cppb::vector<std::string> get_library_libs(std::string_view library, config const &config)
{
	auto const libs = [&]() {
		if (library.substr(0, 4) == "llvm" || library.substr(0, 4) == "LLVM")
		{
			auto const llvm_config = [&]() -> std::string {
				if (!config.llvm_config_path.empty())
				{
					return config.llvm_config_path;
				}
				else if (library.size() == 4)
				{
					return "llvm-config";
				}
				else
				{
					return "llvm-config" + std::string(library.substr(4));
				}
			}();
			auto [flags, is_good] = capture_command_output(llvm_config, {{ "--ldflags", "--libs", "--system-libs" }});
			return is_good ? std::move(flags) : "";
		}
		else
		{
			auto [flags, is_good] = capture_command_output("pkg-config", {{ "--libs", std::string(library) }});
			return is_good ? std::move(flags) : fmt::format("-l{}", library);
		}
	}();
	std::string_view remaining = libs;
	cppb::vector<std::string> result;
	while (remaining.size() != 0)
	{
		auto const next_whitespace_pos = remaining.find_first_of(" \n\t");
		auto const flag = remaining.substr(0, next_whitespace_pos);
		if (flag != "")
		{
			result.emplace_back(flag);
		}
		if (next_whitespace_pos == std::string_view::npos)
		{
			break;
		}
		remaining = remaining.substr(next_whitespace_pos + 1);
	}
	return result;
}

template<auto config::*member, auto config_is_set::*is_set_member>
static void fill_config_member(rapidjson::Value::ConstObject object, char const *name, config &config, config_is_set &config_is_set, std::string &error)
{
	static_assert(member != nullptr);
	static_assert(is_set_member != nullptr);

	if (auto const it = object.FindMember(name); it != object.MemberEnd())
	{
		if constexpr (std::is_same_v<decltype(member), bool config::*>)
		{
			if (config_is_set.*is_set_member)
			{
				return;
			}
			if (!it->value.IsBool())
			{
				error = fmt::format("value of member '{}' in configuration file must be a 'Bool'", name);
				return;
			}
			config.*member = it->value.GetBool();
			config_is_set.*is_set_member = true;
		}
		else if constexpr (
			std::is_same_v<decltype(member), fs::path config::*>
			|| std::is_same_v<decltype(member), std::string config::*>
		)
		{
			if (config_is_set.*is_set_member)
			{
				return;
			}
			if (!it->value.IsString())
			{
				error = fmt::format("value of member '{}' in configuration file must be a 'String'", name);
				return;
			}
			config.*member = it->value.GetString();
			config_is_set.*is_set_member = true;
		}
		else
		{
			static_assert(
				std::is_same_v<decltype(member), cppb::vector<fs::path> config::*>
				|| std::is_same_v<decltype(member), cppb::vector<std::string> config::*>
			);
			if (!it->value.IsArray())
			{
				error = fmt::format("value of member '{}' in configuration file must be an 'Array'", name);
				return;
			}
			auto const array = it->value.GetArray();
			for (auto const &elem : array)
			{
				if (!elem.IsString())
				{
					error = fmt::format("array member in value of member '{}' in configuration file must be a 'String'", name);
					return;
				}
				(config.*member).emplace_back(elem.GetString());
			}
			config_is_set.*is_set_member = true;
		}
	}
}

static int parse_int(std::string_view str)
{
	int result = 0;
	for (auto const c : str)
	{
		result *= 10;
		result += c - '0';
	}
	return result;
}

static void fill_config(rapidjson::Value::ConstObject object, config &config, config_is_set &config_is_set, std::string &error)
{
	if (!config_is_set.compiler)
	{
		if (auto const compiler_it = object.FindMember("compiler"); compiler_it != object.MemberEnd())
		{
			if (!compiler_it->value.IsString())
			{
				error = "value of member 'compiler' in configuration file must be a 'String'";
				return;
			}
			std::string_view const compiler = compiler_it->value.GetString();
			if (compiler.starts_with("gcc") || compiler.starts_with("g++") || compiler.starts_with("GCC"))
			{
				config.compiler = compiler_kind::gcc;
				config_is_set.compiler = true;
				auto const version_string = compiler.substr(3);
				if (!version_string.empty() && !(
					version_string.starts_with('-')
					&& version_string.size() > 1
					&& ranges::basic_range{ version_string.begin() + 1, version_string.end() }
						.is_all([](auto const c) { return c >= '0' && c <= '9'; })
				))
				{
					error = fmt::format("invalid version specifier '{}' for member 'compiler' in configuration file", version_string);
					return;
				}
				else if (!version_string.empty())
				{
					config.compiler_version = parse_int(version_string.substr(1));
				}
			}
			else if (compiler.starts_with("clang") /* || compiler.starts_with("clang++") */ || compiler.starts_with("Clang"))
			{
				config.compiler = compiler_kind::clang;
				config_is_set.compiler = true;
				auto const version_string = compiler.starts_with("clang++") ? compiler.substr(7) : compiler.substr(5);
				if (!version_string.empty() && !(
					version_string.starts_with('-')
					&& version_string.size() > 1
					&& ranges::basic_range{ version_string.begin() + 1, version_string.end() }
						.is_all([](auto const c) { return c >= '0' && c <= '9'; })
				))
				{
					error = fmt::format("invalid version specifier '{}' for member 'compiler' in configuration file", version_string);
					return;
				}
				else if (!version_string.empty())
				{
					config.compiler_version = parse_int(version_string.substr(1));
				}
			}
			else
			{
				error = fmt::format("invalid value '{}' for member 'compiler' in configuration file", compiler);
				return;
			}
		}
	}

#define fill_regular_config_member(member) \
fill_config_member<&config::member, &config_is_set::member>(object, #member, config, config_is_set, error)
#define fill_array_config_member(member) \
fill_config_member<&config::member, &config_is_set::member>(object, #member, config, config_is_set, error)

	fill_regular_config_member(c_compiler_path);
	if (!error.empty()) { return; }
	fill_regular_config_member(cpp_compiler_path);
	if (!error.empty()) { return; }
	fill_regular_config_member(c_standard);
	if (!error.empty()) { return; }
	fill_regular_config_member(cpp_standard);
	if (!error.empty()) { return; }

	fill_regular_config_member(cpp_precompiled_header);
	if (!error.empty()) { return; }
	fill_regular_config_member(c_precompiled_header);
	if (!error.empty()) { return; }

	fill_array_config_member(c_compiler_flags);
	if (!error.empty()) { return; }
	fill_array_config_member(cpp_compiler_flags);
	if (!error.empty()) { return; }
	fill_array_config_member(link_flags);
	if (!error.empty()) { return; }
	fill_array_config_member(libraries);
	if (!error.empty()) { return; }

	fill_regular_config_member(output_name);
	if (!error.empty()) { return; }

	fill_regular_config_member(llvm_config_path);
	if (!error.empty()) { return; }

	fill_array_config_member(run_args);
	if (!error.empty()) { return; }

	fill_regular_config_member(source_directory);
	if (!error.empty()) { return; }
	fill_array_config_member(excluded_sources);
	if (!error.empty()) { return; }

	fill_array_config_member(include_paths);
	if (!error.empty()) { return; }

	fill_array_config_member(defines);
	if (!error.empty()) { return; }
	fill_array_config_member(warnings);
	if (!error.empty()) { return; }

	fill_array_config_member(prebuild_rules);
	if (!error.empty()) { return; }
	fill_array_config_member(prelink_rules);
	if (!error.empty()) { return; }
	fill_array_config_member(postbuild_rules);
	if (!error.empty()) { return; }

	fill_array_config_member(link_dependencies);
	if (!error.empty()) { return; }

	fill_regular_config_member(optimization);
	if (!error.empty()) { return; }
	fill_regular_config_member(emit_compile_commands);
	if (!error.empty()) { return; }

#undef fill_regular_config_member
#undef fill_array_config_member
}

static void fill_default_config_values(config &values, config_is_set values_is_set)
{
#define fill_default_value(member, default_value) \
do { if (!values_is_set.member) { values.member = default_value; } } while (false)

	fill_default_value(compiler, compiler_kind::gcc);
	fill_default_value(c_standard, "c11");
	fill_default_value(cpp_standard, "c++20");
	fill_default_value(source_directory, "src");

#undef fill_default_value
}

static void fill_default_config_values_with(config &values, config_is_set values_is_set, config const &source_values)
{
#define fill_default_value(member) \
do { if (!values_is_set.member) { values.member = source_values.member; } } while (false)

	fill_default_value(compiler);
	if (!values_is_set.compiler) { values.compiler_version = source_values.compiler_version; }
	fill_default_value(c_compiler_path);
	fill_default_value(cpp_compiler_path);
	fill_default_value(c_standard);
	fill_default_value(cpp_standard);

	fill_default_value(cpp_precompiled_header);
	fill_default_value(c_precompiled_header);

	fill_default_value(c_compiler_flags);
	fill_default_value(cpp_compiler_flags);
	fill_default_value(link_flags);
	fill_default_value(libraries);

	fill_default_value(output_name);

	fill_default_value(llvm_config_path);

	fill_default_value(run_args);

	fill_default_value(source_directory);
	fill_default_value(excluded_sources);

	fill_default_value(include_paths);

	fill_default_value(defines);
	fill_default_value(warnings);

	fill_default_value(prebuild_rules);
	fill_default_value(prelink_rules);
	fill_default_value(postbuild_rules);

	fill_default_value(link_dependencies);

	fill_default_value(optimization);
	fill_default_value(emit_compile_commands);

#undef fill_default_value
}

enum class resolve_state
{
	none, resolving, all,
	circular_error, error,
};

struct config_object_pair
{
	std::string_view name;
	rapidjson::Value::ConstObject json_object;
	project_config *config;
	resolve_state state;
};

static void resolve_project_config(
	config_object_pair &config_object,
	cppb::span<config_object_pair> configs,
	std::string &error
)
{
	auto &config = *config_object.config;
	project_config_is_set is_set{};
	auto const &object = config_object.json_object;
	if (config_object.state == resolve_state::all)
	{
		return;
	}
	else if (config_object.state == resolve_state::resolving)
	{
		error = fmt::format("circular dependency encountered in config file; '{}'", config_object.name);
		config_object.state = resolve_state::circular_error;
		return;
	}
	config_object.state = resolve_state::resolving;
	config.project_name = config_object.name;

	project_config *depends_on_config = nullptr;
	if (
		auto const depends_on_it = config_object.json_object.FindMember("depends_on");
		depends_on_it != config_object.json_object.MemberEnd()
	)
	{
		if (!depends_on_it->value.IsString())
		{
			error = "value of member 'depends_on' in configuration file must be a 'String'";
			config_object.state = resolve_state::error;
			return;
		}
		auto const it = std::find_if(
			configs.begin(), configs.end(),
			[depends_on_name = depends_on_it->value.GetString()](auto const &config_) {
				return config_.name == depends_on_name;
			}
		);
		if (it == configs.end())
		{
			error = fmt::format(
				"invalid value '{}' for member 'depends_on', there's no such configuration",
				depends_on_it->value.GetString()
			);
			config_object.state = resolve_state::error;
			return;
		}

		resolve_project_config(*it, configs, error);
		if (it->state == resolve_state::circular_error)
		{
			error += fmt::format(" required by '{}'", config_object.name);
			config_object.state = resolve_state::circular_error;
			return;
		}

		depends_on_config = it->config;
	}

	if (auto const configs_it = object.FindMember("configs"); configs_it != object.MemberEnd())
	{
		if (!configs_it->value.IsObject())
		{
			error = "value of member 'configs' in configuration file must be an 'Object'";
			config_object.state = resolve_state::error;
			return;
		}
		auto const configs_object = configs_it->value.GetObject();

		if (auto const windows_it = configs_object.FindMember("windows-debug"); windows_it != configs_object.MemberEnd())
		{
			if (!windows_it->value.IsObject())
			{
				error = "value of member 'windows-debug' in configuration file must be an 'Object'";
				config_object.state = resolve_state::error;
				return;
			}
			fill_config(windows_it->value.GetObject(), config.windows_debug, is_set.windows_debug, error);
			if (!error.empty())
			{
				config_object.state = resolve_state::error;
				return;
			}
		}
		if (auto const windows_it = configs_object.FindMember("windows-release"); windows_it != configs_object.MemberEnd())
		{
			if (!windows_it->value.IsObject())
			{
				error = "value of member 'windows-release' in configuration file must be an 'Object'";
				config_object.state = resolve_state::error;
				return;
			}
			fill_config(windows_it->value.GetObject(), config.windows_release, is_set.windows_release, error);
			if (!error.empty())
			{
				config_object.state = resolve_state::error;
				return;
			}
		}
		if (auto const windows_it = configs_object.FindMember("linux-debug"); windows_it != configs_object.MemberEnd())
		{
			if (!windows_it->value.IsObject())
			{
				error = "value of member 'linux-debug' in configuration file must be an 'Object'";
				config_object.state = resolve_state::error;
				return;
			}
			fill_config(windows_it->value.GetObject(), config.linux_debug, is_set.linux_debug, error);
			if (!error.empty())
			{
				config_object.state = resolve_state::error;
				return;
			}
		}
		if (auto const windows_it = configs_object.FindMember("linux-release"); windows_it != configs_object.MemberEnd())
		{
			if (!windows_it->value.IsObject())
			{
				error = "value of member 'linux-release' in configuration file must be an 'Object'";
				config_object.state = resolve_state::error;
				return;
			}
			fill_config(windows_it->value.GetObject(), config.linux_release, is_set.linux_release, error);
			if (!error.empty())
			{
				config_object.state = resolve_state::error;
				return;
			}
		}

		if (auto const debug_it = configs_object.FindMember("debug"); debug_it != configs_object.MemberEnd())
		{
			if (!debug_it->value.IsObject())
			{
				error = "value of member 'debug' in configuration file must be an 'Object'";
				config_object.state = resolve_state::error;
				return;
			}
			fill_config(debug_it->value.GetObject(), config.windows_debug, is_set.windows_debug, error);
			if (!error.empty())
			{
				config_object.state = resolve_state::error;
				return;
			}
			fill_config(debug_it->value.GetObject(), config.linux_debug, is_set.linux_debug, error);
			if (!error.empty())
			{
				config_object.state = resolve_state::error;
				return;
			}
		}
		if (auto const release_it = configs_object.FindMember("release"); release_it != configs_object.MemberEnd())
		{
			if (!release_it->value.IsObject())
			{
				error = "value of member 'release' in configuration file must be an 'Object'";
				config_object.state = resolve_state::error;
				return;
			}
			fill_config(release_it->value.GetObject(), config.windows_release, is_set.windows_release, error);
			if (!error.empty())
			{
				config_object.state = resolve_state::error;
				return;
			}
			fill_config(release_it->value.GetObject(), config.linux_release, is_set.linux_release, error);
			if (!error.empty())
			{
				config_object.state = resolve_state::error;
				return;
			}
		}

		if (auto const windows_it = configs_object.FindMember("windows"); windows_it != configs_object.MemberEnd())
		{
			if (!windows_it->value.IsObject())
			{
				error = "value of member 'windows' in configuration file must be an 'Object'";
				config_object.state = resolve_state::error;
				return;
			}
			fill_config(windows_it->value.GetObject(), config.windows_debug, is_set.windows_debug, error);
			if (!error.empty())
			{
				config_object.state = resolve_state::error;
				return;
			}
			fill_config(windows_it->value.GetObject(), config.windows_release, is_set.windows_release, error);
			if (!error.empty())
			{
				config_object.state = resolve_state::error;
				return;
			}
		}
		if (auto const linux_it = configs_object.FindMember("linux"); linux_it != configs_object.MemberEnd())
		{
			if (!linux_it->value.IsObject())
			{
				error = "value of member 'linux' in configuration file must be an 'Object'";
				config_object.state = resolve_state::error;
				return;
			}
			fill_config(linux_it->value.GetObject(), config.linux_debug, is_set.linux_debug, error);
			if (!error.empty())
			{
				config_object.state = resolve_state::error;
				return;
			}
			fill_config(linux_it->value.GetObject(), config.linux_release, is_set.linux_release, error);
			if (!error.empty())
			{
				config_object.state = resolve_state::error;
				return;
			}
		}
	}

	fill_config(object, config.windows_debug, is_set.windows_debug, error);
	if (!error.empty())
	{
		config_object.state = resolve_state::error;
		return;
	}
	fill_config(object, config.windows_release, is_set.windows_release, error);
	if (!error.empty())
	{
		config_object.state = resolve_state::error;
		return;
	}
	fill_config(object, config.linux_debug, is_set.linux_debug, error);
	if (!error.empty())
	{
		config_object.state = resolve_state::error;
		return;
	}
	fill_config(object, config.linux_release, is_set.linux_release, error);
	if (!error.empty())
	{
		config_object.state = resolve_state::error;
		return;
	}

	config_object.state = resolve_state::all;

	if (depends_on_config == nullptr)
	{
		fill_default_config_values(config.windows_debug,   is_set.windows_debug);
		fill_default_config_values(config.windows_release, is_set.windows_release);
		fill_default_config_values(config.linux_debug,   is_set.linux_debug);
		fill_default_config_values(config.linux_release, is_set.linux_release);
		if (!is_set.windows_debug.optimization)
		{
			config.windows_debug.optimization = "0";
		}
		if (!is_set.windows_release.optimization)
		{
			config.windows_release.optimization = "3";
		}
		if (!is_set.linux_debug.optimization)
		{
			config.linux_debug.optimization = "0";
		}
		if (!is_set.linux_release.optimization)
		{
			config.linux_release.optimization = "3";
		}
	}
	else
	{
		fill_default_config_values_with(config.windows_debug,   is_set.windows_debug,   depends_on_config->windows_debug);
		fill_default_config_values_with(config.windows_release, is_set.windows_release, depends_on_config->windows_release);
		fill_default_config_values_with(config.linux_debug,   is_set.linux_debug,   depends_on_config->linux_debug);
		fill_default_config_values_with(config.linux_release, is_set.linux_release, depends_on_config->linux_release);
	}
}

static cppb::vector<project_config> get_project_configs(rapidjson::Document::Object object, std::string &error)
{
	cppb::vector<config_object_pair> config_objects = [&]() {
		cppb::vector<config_object_pair> result;
		auto const begin = object.begin();
		auto const end   = object.end();
		for (auto it = begin; it != end; ++it)
		{
			auto const &member = *it;
			assert(member.name.IsString());
			std::string_view const name = member.name.GetString();
			if (!member.value.IsObject())
			{
				error = fmt::format("configuration value for member '{}' must be an 'Object'", name);
				return result;
			}
			result.push_back({ name, member.value.GetObject(), nullptr, resolve_state::none });
		}
		return result;
	}();
	if (!error.empty())
	{
		return {};
	}
	cppb::vector<project_config> result{};
	result.resize(config_objects.size());

	for (std::size_t i = 0; i < config_objects.size(); ++i)
	{
		config_objects[i].config = &result[i];
	}

	for (auto &config_object : config_objects)
	{
		resolve_project_config(config_object, config_objects, error);
		if (!error.empty())
		{
			return {};
		}
	}

	return result;
}

static cppb::vector<std::string> get_string_array(rapidjson::Document::Array array, std::string &error)
{
	cppb::vector<std::string> result;
	result.reserve(array.Size());
	for (auto const &element : array)
	{
		if (!element.IsString())
		{
			error = "array element must be a 'String'";
			return {};
		}
		result.push_back(element.GetString());
	}

	return result;
}

static os_specific_rule get_os_specific_rule(rapidjson::Document::Object object, std::string &error)
{
	os_specific_rule result;

	auto const end = object.MemberEnd();
	// dependencies

	auto const dependencies_it = object.FindMember("dependencies");
	if (dependencies_it != end)
	{
		if (!dependencies_it->value.IsArray())
		{
			error = "value for member 'dependencies' must be an 'Array'";
			return {};
		}
		result.dependencies = get_string_array(dependencies_it->value.GetArray(), error);
		if (!error.empty())
		{
			return {};
		}
	}

	// command or commands
	auto const command_it = object.FindMember("command");
	auto const commands_it = object.FindMember("commands");
	if (command_it != end && commands_it != end)
	{
		error = "only one of 'command' or 'commands' may be provided in a rule";
		return {};
	}
	else if (command_it != end)
	{
		if (!command_it->value.IsString())
		{
			error = "value for member 'command' must be a 'String'";
			return {};
		}
		result.commands.push_back(command_it->value.GetString());
	}
	else if (commands_it != end)
	{
		if (!commands_it->value.IsArray())
		{
			error = "value for member 'commands' must be an 'Array'";
			return {};
		}
		result.commands = get_string_array(commands_it->value.GetArray(), error);
		if (!error.empty())
		{
			return {};
		}
	}

	// is_file
	auto const is_file_it = object.FindMember("is_file");
	if (is_file_it != end)
	{
		if (!is_file_it->value.IsBool())
		{
			error = "value for member 'is_file' must be a 'Bool'";
			return {};
		}
		result.is_file = is_file_it->value.GetBool();
	}

	return result;
}

static rule get_rule(std::string_view name, rapidjson::Document::Object object, std::string &error)
{
	rule result;
	result.rule_name = name;

	auto const windows_it = object.FindMember("windows");
	auto const linux_it = object.FindMember("linux");
	if (windows_it == object.MemberEnd() && linux_it == object.MemberEnd())
	{
		result.windows_rule = get_os_specific_rule(object, error);
		result.linux_rule = result.windows_rule;
	}
	else if (windows_it == object.MemberEnd() || linux_it == object.MemberEnd())
	{
		if (windows_it == object.MemberEnd())
		{
			error = "member 'windows' must be specified when 'linux' is also specified";
		}
		else
		{
			error = "member 'linux' must be specified when 'windows' is also specified";
		}
		return {};
	}
	else
	{
		if (!windows_it->value.IsObject())
		{
			error = "value for member 'windows' must be an 'Object'";
			return {};
		}
		if (!linux_it->value.IsObject())
		{
			error = "value for member 'windows' must be an 'Object'";
			return {};
		}

		auto const windows_object = windows_it->value.GetObject();
		result.windows_rule = get_os_specific_rule(windows_object, error);

		auto const linux_object = linux_it->value.GetObject();
		result.linux_rule = get_os_specific_rule(linux_object, error);
	}

	return result;
}

static cppb::vector<rule> get_rules(rapidjson::Document::Object object, std::string &error)
{
	cppb::vector<rule> result;

	result.reserve(object.MemberCount());
	auto it = object.MemberBegin();
	auto const end = object.MemberEnd();
	for (; it != end; ++it)
	{
		auto &[name, value] = *it;

		assert(name.IsString());
		if (!value.IsObject())
		{
			error = "array element in 'rules' must be an 'Object'";
			return {};
		}

		result.push_back(get_rule(name.GetString(), value.GetObject(), error));
		if (!error.empty())
		{
			return {};
		}
	}

	return result;
}

config_file read_config_json(fs::path const &dep_file_path, std::string &error)
{
	std::ifstream input(dep_file_path);
	if (!input.is_open())
	{
		error = fmt::format("could not open file '{}'", dep_file_path.generic_string());
		return {};
	}

	rapidjson::IStreamWrapper input_wrapper(input);
	rapidjson::Document document;
	document.ParseStream(input_wrapper);

	if (document.HasParseError())
	{
		error = fmt::format("an error occurred while parsing '{}'", dep_file_path.generic_string());
		return {};
	}

	if (!document.IsObject())
	{
		error = "top level value in configuration file must be an 'Object'";
		return {};
	}

	auto const object = document.GetObject();
	auto result = config_file{};

	auto const projects_object_it = object.FindMember("projects");
	if (projects_object_it == object.MemberEnd())
	{
		error = "unable to find 'projects' field in configuration file";
		return {};
	}

	if (!projects_object_it->value.IsObject())
	{
		error = "configuration value for member 'projects' must be an 'Object'";
		return {};
	}

	result.projects = get_project_configs(projects_object_it->value.GetObject(), error);
	if (!error.empty())
	{
		return {};
	}

	auto const rules_object_it = object.FindMember("rules");
	if (rules_object_it == object.MemberEnd())
	{
		return result;
	}

	if (!rules_object_it->value.IsObject())
	{
		error = "configuration value for member 'rules' must be an 'Object'";
		return {};
	}

	result.rules = get_rules(rules_object_it->value.GetObject(), error);
	if (!error.empty())
	{
		return {};
	}

	return result;
}

std::optional<output_file_info> read_output_file_info_json(fs::path const &file_info_json)
{
	std::ifstream input(file_info_json);
	if (!input)
	{
		return std::nullopt;
	}

	rapidjson::IStreamWrapper input_wrapper(input);
	rapidjson::Document document;
	document.ParseStream(input_wrapper);

	if (document.HasParseError())
	{
		return std::nullopt;
	}

	if (!document.IsObject())
	{
		return std::nullopt;
	}

	auto const object = document.GetObject();
	auto result = output_file_info{};

	auto const compiler_it = object.FindMember("compiler");
	if (compiler_it == object.MemberEnd())
	{
		return std::nullopt;
	}

	if (!compiler_it->value.IsString())
	{
		return std::nullopt;
	}

	result.compiler = compiler_it->value.GetString();

	auto const args_it = object.FindMember("args");
	if (args_it == object.MemberEnd())
	{
		return std::nullopt;
	}

	if (!args_it->value.IsArray())
	{
		return std::nullopt;
	}

	for (auto const &arg : args_it->value.GetArray())
	{
		if (!arg.IsString())
		{
			return std::nullopt;
		}

		result.args.push_back(arg.GetString());
	}

	auto const hash_it = object.FindMember("hash");
	if (hash_it == object.MemberEnd())
	{
		return std::nullopt;
	}

	if (!hash_it->value.IsString())
	{
		return std::nullopt;
	}

	result.hash = hash_it->value.GetString();

	return std::move(result);
}

void write_output_file_info_json(
	fs::path const &file_info_json,
	std::string_view compiler,
	cppb::span<std::string const> args,
	std::string_view hash
)
{
	rapidjson::Document document;
	auto &object = document.SetObject();

	object.AddMember(
		"compiler",
		rapidjson::Document::StringRefType(compiler.data(), static_cast<rapidjson::SizeType>(compiler.size())),
		document.GetAllocator()
	);
	assert(object.FindMember("compiler")->value.IsString());

	object.AddMember("args", {}, document.GetAllocator());
	auto &args_array = object.FindMember("args")->value;
	args_array.SetArray();
	for (auto const &arg : args)
	{
		args_array.PushBack(
			rapidjson::Document::StringRefType(arg.data(), static_cast<rapidjson::SizeType>(arg.size())),
			document.GetAllocator()
		);
	}
	assert(object.FindMember("args")->value.IsArray());

	object.AddMember(
		"hash",
		rapidjson::Document::StringRefType(hash.data(), static_cast<rapidjson::SizeType>(hash.size())),
		document.GetAllocator()
	);
	assert(object.FindMember("hash")->value.IsString());

	fs::create_directories(file_info_json.parent_path());
	auto output_file = std::ofstream(file_info_json);
	auto output_wrapper = rapidjson::OStreamWrapper(output_file);
	auto writer = rapidjson::Writer(output_wrapper);
	document.Accept(writer);
}

void add_c_compiler_flags(cppb::vector<std::string> &args, config const &config)
{
	switch (config.compiler)
	{
	case compiler_kind::gcc:
	case compiler_kind::clang:
		for (auto const &library : config.libraries)
		{
			// special case for llvm
			auto const cflags = get_library_cflags(library, config);
			args.append(std::move(cflags));
		}

		args.emplace_back(fmt::format("-std={}", config.c_standard));
		for (auto const &flag : config.c_compiler_flags)
		{
			args.emplace_back(flag);
		}
		for (auto const &include_path : config.include_paths)
		{
			args.emplace_back(fmt::format("-I{}", include_path.generic_string()));
		}
		for (auto const &define : config.defines)
		{
			args.emplace_back(fmt::format("-D{}", define));
		}

		{
			auto warning_indicies = ranges::iota(std::size_t(0), config.warnings.size()).collect<cppb::vector>();
			std::sort(
				warning_indicies.begin(), warning_indicies.end(),
				[&](auto const lhs, auto const rhs) {
					auto const lhs_starts_with = std::string_view(config.warnings[lhs]).substr(0, 3) == "no-";
					auto const rhs_starts_with = std::string_view(config.warnings[rhs]).substr(0, 3) == "no-";
					if (lhs_starts_with != rhs_starts_with)
					{
						return lhs_starts_with ? false : true;
					}
					else
					{
						return lhs < rhs;
					}
				}
			);
			for (auto const i : warning_indicies)
			{
				args.emplace_back(fmt::format("-W{}", config.warnings[i]));
			}
		}

		args.emplace_back(fmt::format("-O{}", config.optimization));
		break;
	}
}

void add_cpp_compiler_flags(cppb::vector<std::string> &args, config const &config)
{
	switch (config.compiler)
	{
	case compiler_kind::gcc:
	case compiler_kind::clang:
		for (auto const &library : config.libraries)
		{
			auto const cxxflags = get_library_cxxflags(library, config);
			args.append(cxxflags);
		}

		args.emplace_back(fmt::format("-std={}", config.cpp_standard));
		for (auto const &flag : config.cpp_compiler_flags)
		{
			args.emplace_back(flag);
		}
		for (auto const &include_path : config.include_paths)
		{
			args.emplace_back(fmt::format("-I{}", include_path.generic_string()));
		}
		for (auto const &define : config.defines)
		{
			args.emplace_back(fmt::format("-D{}", define));
		}

		{
			auto warning_indicies = ranges::iota(std::size_t(0), config.warnings.size()).collect<cppb::vector>();
			std::sort(
				warning_indicies.begin(), warning_indicies.end(),
				[&](auto const lhs, auto const rhs) {
					auto const lhs_starts_with = std::string_view(config.warnings[lhs]).substr(0, 3) == "no-";
					auto const rhs_starts_with = std::string_view(config.warnings[rhs]).substr(0, 3) == "no-";
					if (lhs_starts_with != rhs_starts_with)
					{
						return lhs_starts_with ? false : true;
					}
					else
					{
						return lhs < rhs;
					}
				}
			);
			for (auto const i : warning_indicies)
			{
				args.emplace_back(fmt::format("-W{}", config.warnings[i]));
			}
		}

		args.emplace_back(fmt::format("-O{}", config.optimization));
		break;
	}
}

void add_link_flags(cppb::vector<std::string> &args, config const &config)
{
	switch (config.compiler)
	{
	case compiler_kind::gcc:
	case compiler_kind::clang:
		for (auto const &library : config.libraries)
		{
			auto const libs = get_library_libs(library, config);
			args.append(libs);
		}
		for (auto const &flag : config.link_flags)
		{
			args.emplace_back(flag);
		}
		break;
	}
}

static std::string create_default_config_file_string(std::string_view source_directory)
{
	std::string result =
R"({
	"projects": {
		"default": {
			"compiler": "gcc",
			"c_standard": "c11",
			"cpp_standard": "c++20",

			"c_compiler_flags": [],
			"cpp_compiler_flags": [],
			"link_flags": [],
			"emit_compile_commands": true,

			"run_args": [],

			"source_directory": ")";
		result += source_directory;
		result += R"(",
			"excluded_sources": [],
			"include_paths": [],
			"libraries": [],

			"defines": [],
			"warnings": [ "all", "extra" ],

			"prebuild_rules": [],
			"prelink_rules": [],
			"postbuild_rules": [],

			"configs": {
				"windows-debug": {},
				"windows-release": {},
				"linux-debug": {},
				"linux-release": {},
				"windows": {},
				"linux": {},
				"debug": {
					"optimization": "0"
				},
				"release": {
					"defines": [ "NDEBUG" ],
					"optimization": "3"
				}
			}
		}
	},
	"rules": {}
}
)";
	return result;
}

void output_default_config_json(fs::path const &config_path, std::string_view source_directory)
{
	std::ofstream config_file(config_path);
	if (!config_file.is_open())
	{
		return;
	}

	config_file << create_default_config_file_string(source_directory);
}
