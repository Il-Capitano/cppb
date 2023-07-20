#ifndef CONFIG_H
#define CONFIG_H

#include "core.h"

enum class compiler_kind
{
	gcc, clang,
};

struct config
{
	compiler_kind compiler = compiler_kind::gcc;
	int compiler_version = -1;
	fs::path c_compiler_path;
	fs::path cpp_compiler_path;
	std::string c_standard;
	std::string cpp_standard;

	fs::path cpp_precompiled_header;
	fs::path c_precompiled_header;

	cppb::vector<std::string> c_compiler_flags;
	cppb::vector<std::string> cpp_compiler_flags;
	cppb::vector<std::string> link_flags;
	cppb::vector<std::string> libraries;

	std::string llvm_config_path;

	cppb::vector<std::string> run_args;

	fs::path source_directory;
	cppb::vector<fs::path> excluded_sources;

	cppb::vector<fs::path> include_paths;

	cppb::vector<std::string> defines;
	cppb::vector<std::string> warnings;

	cppb::vector<std::string> prebuild_rules;
	cppb::vector<std::string> prelink_rules;
	cppb::vector<std::string> postbuild_rules;

	cppb::vector<fs::path> link_dependencies;

	std::string optimization;
	bool emit_compile_commands = false;
};

struct config_is_set
{
	bool compiler          = false;
	bool c_compiler_path   = false;
	bool cpp_compiler_path = false;
	bool c_standard        = false;
	bool cpp_standard      = false;

	bool cpp_precompiled_header;
	bool c_precompiled_header;

	bool c_compiler_flags   = false;
	bool cpp_compiler_flags = false;
	bool link_flags         = false;
	bool libraries          = false;

	bool llvm_config_path = false;

	bool run_args = false;

	bool source_directory = false;
	bool excluded_sources = false;

	bool include_paths = false;

	bool defines  = false;
	bool warnings = false;

	bool prebuild_rules  = false;
	bool prelink_rules   = false;
	bool postbuild_rules = false;

	bool link_dependencies = false;

	bool optimization          = false;
	bool emit_compile_commands = false;
};

struct project_config
{
	std::string project_name;
	config windows_debug;
	config windows_release;
	config linux_debug;
	config linux_release;
};

struct project_config_is_set
{
	config_is_set windows_debug;
	config_is_set windows_release;
	config_is_set linux_debug;
	config_is_set linux_release;
};

struct rule
{
	std::string               rule_name{};
	cppb::vector<std::string> dependencies{};
	cppb::vector<std::string> commands{};
	bool                      is_file = true;
};

struct config_file
{
	cppb::vector<project_config> projects{};
	cppb::vector<rule>           rules{};
};

config_file read_config_json(fs::path const &config_file_path, std::string &error);
void add_c_compiler_flags(cppb::vector<std::string> &args, config const &config);
void add_cpp_compiler_flags(cppb::vector<std::string> &args, config const &config);
void add_link_flags(cppb::vector<std::string> &args, config const &config);

void output_default_config_json(fs::path const &config_path, std::string_view source_directory);

#endif // CONFIG_H
