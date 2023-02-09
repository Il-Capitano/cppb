#ifndef PROCESS_H
#define PROCESS_H

#include "core.h"

struct process_result
{
	int error_count = 0;
	int warning_count = 0;
	int exit_code = 0;
	std::string captured_output;
};

enum class output_kind
{
	stdout_, stderr_, capture
};

process_result run_command(std::string_view command, output_kind output);
process_result run_command(std::string_view executable, cppb::vector<std::string> const &arguments, output_kind output);
std::pair<std::string, bool> capture_command_output(std::string_view executable, cppb::vector<std::string> const &arguments);

#endif // PROCESS_H
