#include "process.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>

static process_result run_process(std::string command, output_kind output)
{
	FILE *output_file = nullptr;
	if (output == output_kind::stdout_)
	{
		// redirect stdout to stderr for popen to not capture it
		output_file = stdout;
#ifdef _WIN32
		command += " 1>&2";
#else
		command += " 1>&2";
#endif // windows
	}
	else if (output == output_kind::stderr_)
	{
		output_file = stderr;
#ifdef _WIN32
		command += " 1>&2";
#else
		command += " 1>&2";
#endif // windows
	}
	else if (output == output_kind::null_)
	{
		// redirect stderr to stdout for popen to capture it
#ifdef _WIN32
		command += " 2>&1";
#else
		command += " 2>&1";
#endif // windows
	}

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

	process_result result = {};
	auto const process = popen(command.c_str(), "r");
	if (process == nullptr)
	{
		fmt::print("popen failed!\n");
		result.exit_code = -1;
		return result;
	}

	if (output_file == nullptr)
	{
		char buffer[1024];
		while (std::fgets(buffer, sizeof buffer, process) != nullptr)
		{
			auto const buffer_sv = std::string_view(buffer);
			if (buffer_sv.find("error:") != std::string_view::npos)
			{
				result.error_count += 1;
			}
			else if (buffer_sv.find("warning:") != std::string_view::npos)
			{
				result.warning_count += 1;
			}
		}
	}

	auto const exit_code = pclose(process);
#ifdef _WIN32
	result.exit_code = exit_code;
#else
	result.exit_code = WEXITSTATUS(exit_code);
#endif // windows
	return result;
}

process_result run_command(std::string_view executable, cppb::vector<std::string> arguments, output_kind output)
{
	std::string command{ executable };
	for (auto const &arg : arguments)
	{
		command += ' ';
		command += arg;
	}

	return run_process(std::move(command), output);
}
