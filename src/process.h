#ifndef PROCESS_H
#define PROCESS_H

#include "core.h"

enum class output_kind
{
	stdout_, stderr_, null_,
};

int run_command(std::string_view executable, cppb::vector<std::string> arguments, output_kind output);

#endif // PROCESS_H
