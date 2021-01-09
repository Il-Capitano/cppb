#ifndef PROCESS_H
#define PROCESS_H

#include "core.h"

struct process_result
{
	int error_count = 0;
	int warning_count = 0;
	int exit_code = 0;
};

enum class output_kind
{
	stdout_, stderr_, null_,
};

process_result run_command(std::string_view executable, cppb::vector<std::string> arguments, output_kind output);

#endif // PROCESS_H
