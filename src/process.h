#ifndef PROCESS_H
#define PROCESS_H

#include "core.h"
#include <span>

struct process_result
{
	int error_count = 0;
	int warning_count = 0;
	int exit_code = 0;
	std::string stdout_string;
	std::string stderr_string;
};

std::string make_command_string(std::string_view command, cppb::span<std::string const> args);
process_result run_command(std::string_view command, bool capture);
process_result run_command(std::string_view executable, cppb::vector<std::string> const &arguments, bool capture);
std::pair<std::string, bool> capture_command_output(std::string_view executable, cppb::vector<std::string> const &arguments);

#endif // PROCESS_H
