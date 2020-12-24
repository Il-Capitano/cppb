#include "config.h"
#include <filesystem>
#include <fstream>
#include <rapidjson/document.h>
#include <rapidjson/error/error.h>
#include <rapidjson/writer.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/ostreamwrapper.h>
#include <rapidjson/istreamwrapper.h>

template<auto config::*member, auto config_is_set::*is_set_member>
static void fill_config_member(rapidjson::Value::ConstObject object, char const *name, config &config, config_is_set &config_is_set, std::string &error)
{
	static_assert(member != nullptr);

	if (is_set_member != nullptr && config_is_set.*is_set_member)
	{
		return;
	}

	if (auto const it = object.FindMember(name); it != object.MemberEnd())
	{
		if constexpr (std::is_same_v<decltype(member), bool config::*>)
		{
			static_assert(is_set_member != nullptr);
			if (!it->value.IsBool())
			{
				error = fmt::format("value of member '{}' in configuration file must be a 'Bool'", name);
				return;
			}
			config.*member = it->value.GetBool();
			config_is_set.*is_set_member = true;
		}
		else if constexpr (is_set_member != nullptr)
		{
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
		}
	}
}

static void fill_config(rapidjson::Value::ConstObject object, config &config, config_is_set &config_is_set, std::string &error)
{
	if (auto const compiler_it = object.FindMember("compiler"); compiler_it != object.MemberEnd())
	{
		if (!compiler_it->value.IsString())
		{
			error = "value of member 'compiler' in configuration file must be a 'String'";
			return;
		}
		std::string_view const compiler = compiler_it->value.GetString();
		if (compiler == "gcc" || compiler == "g++" || compiler == "GCC")
		{
			config.compiler = compiler_kind::gcc;
			config_is_set.compiler = true;
		}
		else if (compiler == "clang" || compiler == "clang++" || compiler == "Clang")
		{
			config.compiler = compiler_kind::clang;
			config_is_set.compiler = true;
		}
		else
		{
			error = fmt::format("invalid value '{}' for member 'compiler' in configuration file", compiler);
			return;
		}
	}

#define fill_regular_config_member(member) \
fill_config_member<&config::member, &config_is_set::member>(object, #member, config, config_is_set, error)
#define fill_array_config_member(member) \
fill_config_member<&config::member, static_cast<bool config_is_set::*>(nullptr)>(object, #member, config, config_is_set, error)

	fill_regular_config_member(c_compiler_path);
	if (!error.empty()) { return; }
	fill_regular_config_member(cpp_compiler_path);
	if (!error.empty()) { return; }
	fill_regular_config_member(c_standard);
	if (!error.empty()) { return; }
	fill_regular_config_member(cpp_standard);
	if (!error.empty()) { return; }

	fill_array_config_member(c_compiler_flags);
	if (!error.empty()) { return; }
	fill_array_config_member(cpp_compiler_flags);
	if (!error.empty()) { return; }
	fill_array_config_member(link_flags);
	if (!error.empty()) { return; }

	fill_array_config_member(run_args);
	if (!error.empty()) { return; }

	fill_regular_config_member(source_directory);
	if (!error.empty()) { return; }

	fill_array_config_member(include_paths);
	if (!error.empty()) { return; }
	fill_array_config_member(library_paths);
	if (!error.empty()) { return; }
	fill_array_config_member(libraries);
	if (!error.empty()) { return; }

	fill_array_config_member(defines);
	if (!error.empty()) { return; }
	fill_array_config_member(warnings);
	if (!error.empty()) { return; }
	fill_regular_config_member(optimization);
	if (!error.empty()) { return; }
	fill_regular_config_member(emit_compile_commands);
	if (!error.empty()) { return; }

#undef fill_regular_config_member
#undef fill_array_config_member
}

static void fill_default_config_values(config &config, config_is_set config_is_set)
{
#define fill_default_value(member, default_value) \
do { if (!config_is_set.member) { config.member = default_value; } } while (false)

	fill_default_value(compiler, compiler_kind::gcc);
	fill_default_value(c_standard, "c11");
	fill_default_value(cpp_standard, "c++20");
	fill_default_value(source_directory, "src");

#undef fill_default_value
}

static project_config make_project_config(std::string_view name, rapidjson::Value::ConstObject object, std::string &error)
{
	project_config        result{};
	project_config_is_set result_is_set{};
	result.project_name = name;

	if (auto const configs_it = object.FindMember("configs"); configs_it != object.MemberEnd())
	{
		if (!configs_it->value.IsObject())
		{
			error = "value of member 'configs' in configuration file must be an 'Object'";
			return {};
		}
		auto const configs = configs_it->value.GetObject();

		if (auto const windows_it = configs.FindMember("windows-debug"); windows_it != configs.MemberEnd())
		{
			if (!windows_it->value.IsObject())
			{
				error = "value of member 'windows-debug' in configuration file must be an 'Object'";
				return {};
			}
			fill_config(windows_it->value.GetObject(), result.windows_debug, result_is_set.windows_debug, error);
			if (!error.empty())
			{
				return {};
			}
		}
		if (auto const windows_it = configs.FindMember("windows-release"); windows_it != configs.MemberEnd())
		{
			if (!windows_it->value.IsObject())
			{
				error = "value of member 'windows-release' in configuration file must be an 'Object'";
				return {};
			}
			fill_config(windows_it->value.GetObject(), result.windows_release, result_is_set.windows_release, error);
			if (!error.empty())
			{
				return {};
			}
		}
		if (auto const windows_it = configs.FindMember("linux-debug"); windows_it != configs.MemberEnd())
		{
			if (!windows_it->value.IsObject())
			{
				error = "value of member 'linux-debug' in configuration file must be an 'Object'";
				return {};
			}
			fill_config(windows_it->value.GetObject(), result.linux_debug, result_is_set.linux_debug, error);
			if (!error.empty())
			{
				return {};
			}
		}
		if (auto const windows_it = configs.FindMember("linux-release"); windows_it != configs.MemberEnd())
		{
			if (!windows_it->value.IsObject())
			{
				error = "value of member 'linux-release' in configuration file must be an 'Object'";
				return {};
			}
			fill_config(windows_it->value.GetObject(), result.linux_release, result_is_set.linux_release, error);
			if (!error.empty())
			{
				return {};
			}
		}

		if (auto const debug_it = configs.FindMember("debug"); debug_it != configs.MemberEnd())
		{
			if (!debug_it->value.IsObject())
			{
				error = "value of member 'debug' in configuration file must be an 'Object'";
				return {};
			}
			fill_config(debug_it->value.GetObject(), result.windows_debug, result_is_set.windows_debug, error);
			if (!error.empty())
			{
				return {};
			}
			fill_config(debug_it->value.GetObject(), result.linux_debug, result_is_set.linux_debug, error);
			if (!error.empty())
			{
				return {};
			}
		}
		if (auto const release_it = configs.FindMember("release"); release_it != configs.MemberEnd())
		{
			if (!release_it->value.IsObject())
			{
				error = "value of member 'release' in configuration file must be an 'Object'";
				return {};
			}
			fill_config(release_it->value.GetObject(), result.windows_release, result_is_set.windows_release, error);
			if (!error.empty())
			{
				return {};
			}
			fill_config(release_it->value.GetObject(), result.linux_release, result_is_set.linux_release, error);
			if (!error.empty())
			{
				return {};
			}
		}

		if (auto const windows_it = configs.FindMember("windows"); windows_it != configs.MemberEnd())
		{
			if (!windows_it->value.IsObject())
			{
				error = "value of member 'windows' in configuration file must be an 'Object'";
				return {};
			}
			fill_config(windows_it->value.GetObject(), result.windows_debug, result_is_set.windows_debug, error);
			if (!error.empty())
			{
				return {};
			}
			fill_config(windows_it->value.GetObject(), result.windows_release, result_is_set.windows_release, error);
			if (!error.empty())
			{
				return {};
			}
		}
		if (auto const linux_it = configs.FindMember("linux"); linux_it != configs.MemberEnd())
		{
			if (!linux_it->value.IsObject())
			{
				error = "value of member 'linux' in configuration file must be an 'Object'";
				return {};
			}
			fill_config(linux_it->value.GetObject(), result.linux_debug, result_is_set.linux_debug, error);
			if (!error.empty())
			{
				return {};
			}
			fill_config(linux_it->value.GetObject(), result.linux_release, result_is_set.linux_release, error);
			if (!error.empty())
			{
				return {};
			}
		}
	}

	fill_config(object, result.windows_debug, result_is_set.windows_debug, error);
	if (!error.empty())
	{
		return {};
	}
	fill_config(object, result.windows_release, result_is_set.windows_release, error);
	if (!error.empty())
	{
		return {};
	}
	fill_config(object, result.linux_debug, result_is_set.linux_debug, error);
	if (!error.empty())
	{
		return {};
	}
	fill_config(object, result.linux_release, result_is_set.linux_release, error);
	if (!error.empty())
	{
		return {};
	}

	fill_default_config_values(result.windows_debug,   result_is_set.windows_debug);
	fill_default_config_values(result.windows_release, result_is_set.windows_release);
	fill_default_config_values(result.linux_debug,   result_is_set.linux_debug);
	fill_default_config_values(result.linux_release, result_is_set.linux_release);
	if (!result_is_set.windows_debug.optimization)
	{
		result.windows_debug.optimization = "0";
	}
	if (!result_is_set.windows_release.optimization)
	{
		result.windows_release.optimization = "3";
	}
	if (!result_is_set.linux_debug.optimization)
	{
		result.linux_debug.optimization = "0";
	}
	if (!result_is_set.linux_release.optimization)
	{
		result.linux_release.optimization = "3";
	}
	return result;
}

cppb::vector<project_config> read_config_json(fs::path const &dep_file_path, std::string &error)
{
	std::ifstream input(dep_file_path);
	if (!input.is_open())
	{
		error = fmt::format("could not open file '{}'", dep_file_path.generic_string());
		return {};
	}

	rapidjson::IStreamWrapper input_wrapper(input);
	rapidjson::Document d;
	d.ParseStream(input_wrapper);

	if (d.HasParseError())
	{
		error = fmt::format("an error occurred while parsing '{}'", dep_file_path.generic_string());
		return {};
	}

	if (!d.IsObject())
	{
		error = "top level value in configuration file must be an 'Object'";
		return {};
	}

	cppb::vector<project_config> result{};

	auto const object = d.GetObject();
	auto const begin = object.begin();
	auto const end   = object.end();
	for (auto it = begin; it.operator->() != end.operator->(); ++it)
	{
		auto const &member = *it;
		assert(member.name.IsString());
		std::string_view const name = member.name.GetString();
		if (!member.value.IsObject())
		{
			error = fmt::format("configuration value for member '{}' must be an 'Object'", name);
			return {};
		}
		result.emplace_back(make_project_config(name, member.value.GetObject(), error));
		if (!error.empty())
		{
			return {};
		}
	}

	return result;
}

void add_c_compiler_flags(std::vector<std::string> &args, config const &config)
{
	switch (config.compiler)
	{
	case compiler_kind::gcc:
	case compiler_kind::clang:
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

void add_cpp_compiler_flags(std::vector<std::string> &args, config const &config)
{
	switch (config.compiler)
	{
	case compiler_kind::gcc:
	case compiler_kind::clang:
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

void add_link_flags(std::vector<std::string> &args, config const &config)
{
	switch (config.compiler)
	{
	case compiler_kind::gcc:
	case compiler_kind::clang:
		for (auto const &library_path : config.library_paths)
		{
			args.emplace_back(fmt::format("-L{}", library_path.generic_string()));
		}
		for (auto const &library : config.libraries)
		{
			args.emplace_back(fmt::format("-l{}", library));
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
		"include_paths": [],
		"library_paths": [],
		"libraries": [],

		"defines": [],
		"warnings": [ "all", "extra" ],

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
