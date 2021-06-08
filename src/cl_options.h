#ifndef CL_OPTIONS_H
#define CL_OPTIONS_H

#include <array>
#include "ctcli/ctcli.h"

constexpr auto build_options    = ctcli::options_id_t::_1;
constexpr auto run_options      = build_options;
constexpr auto new_options      = ctcli::options_id_t::_2;
constexpr auto run_rule_options = ctcli::options_id_t::_3;

template<>
inline constexpr bool ctcli::add_verbose_option<build_options> = true;

template<>
inline constexpr std::array ctcli::command_line_options<build_options> = {
	ctcli::create_option("--config-file <path>",         "Set configuration file path (default=.cppb/config.json)", ctcli::arg_type::string),
	ctcli::create_option("--cppb-dir <dir>",             "Set directory used for chaching (default=.cppb)",         ctcli::arg_type::string),
	ctcli::create_option("--bin-dir <dir>",              "Set binary output directory to dir> (default=bin)",       ctcli::arg_type::string),
	ctcli::create_option("--build-config <config>",      "Set which build configuration to use (default=default)",  ctcli::arg_type::string),
	ctcli::create_option("--build-mode {debug|release}", "Set build mode (default=debug)"),
	ctcli::create_option("--rebuild",                    "Rebuild the whole project"),
	ctcli::create_option("--link",                       "Force linking to happen"),
	ctcli::create_option("--jobs <count>",               "Set the number of compiler jobs to run concurrently (default=8)", ctcli::arg_type::uint64),
	ctcli::create_option("-s, --sequential",             "Don't run compilation processes concurrently"),
	ctcli::create_option("--emit-compile-commands",      "Emit a compile_commands.json file"),
};

template<>
inline constexpr std::array ctcli::command_line_options<new_options> = {
	ctcli::create_option("--src-dir <dir>",      "Use <dir> as the source directory (default=src)",         ctcli::arg_type::string),
	ctcli::create_option("--config-file <path>", "Set configuration file path (default=.cppb/config.json)", ctcli::arg_type::string),
};

template<>
inline constexpr std::array ctcli::command_line_options<run_rule_options> = {
	ctcli::create_option("-f, --force",          "Force running of rules, even when the files haven't changed"),
	ctcli::create_option("--config-file <path>", "Set configuration file path (default=.cppb/config.json)", ctcli::arg_type::string),
};

template<>
inline constexpr std::array ctcli::command_line_commands<ctcli::commands_id_t::def> = {
	ctcli::create_command("build", "Build project",         "compiler-flags", build_options),
	ctcli::create_command("run",   "Build and run project", "compiler-flgas", run_options),

	ctcli::create_command("run-rule <rule>",    "Run <rule>",                                           "", run_rule_options, ctcli::arg_type::string),
	ctcli::create_command("new <project-name>", "Create a new project in the directory <project-name>", "", new_options,      ctcli::arg_type::string),
};

enum class build_mode
{
	debug, release,
};

inline std::optional<build_mode> parse_build_mode(std::string_view arg)
{
	if (arg == "debug")
	{
		return build_mode::debug;
	}
	else if (arg == "release")
	{
		return build_mode::release;
	}
	else
	{
		return {};
	}
}

// template<>
// inline constexpr auto ctcli::argument_parse_function<ctcli::option("analyze --build-mode")> = &parse_build_mode;
template<>
inline constexpr auto ctcli::argument_parse_function<ctcli::option("build --build-mode")> = &parse_build_mode;

#endif // CL_OPTIONS_H
