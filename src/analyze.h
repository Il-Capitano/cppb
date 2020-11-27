#ifndef ANALYZE_H
#define ANALYZE_H

#include "core.h"
#include <filesystem>

constexpr cppb::array<std::string_view, 4> source_extensions = {{ ".cpp", ".cxx", ".cc", ".c" }};

struct source_file
{
	fs::path               file_path;
	cppb::vector<fs::path> dependencies;
	fs::file_time_type     last_modified_time;
};

struct compile_command
{
	std::string source_file;
	cppb::vector<std::string> args;
};

cppb::vector<fs::path> get_source_files_in_directory(fs::path const &dir);

void analyze_source_files(
	cppb::vector<fs::path> const &files,
	cppb::vector<fs::path> const &include_directories,
	cppb::vector<source_file> &sources,
	fs::file_time_type dependency_file_last_update,
	fs::file_time_type config_last_update
);

void fill_last_modified_times(cppb::vector<source_file> &sources);

void write_dependency_json(fs::path const &output_path, cppb::vector<source_file> const &sources);
cppb::vector<source_file> read_dependency_json(fs::path const &dep_file_path, std::string &error);

void write_compile_commands_json(cppb::vector<compile_command> const &compile_commands);

#endif // ANALYZE_H
