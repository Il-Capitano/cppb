#include "process.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#endif // windows

static process_result run_process_with_capture(std::string command, output_kind output)
{
	assert(output == output_kind::null_);

	// redirect stderr to stdout for popen to capture it
#ifdef _WIN32
	command += " 2>&1";
#else
	command += " 2>&1";
#endif // windows

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

	auto const exit_code = pclose(process);
#ifdef _WIN32
	result.exit_code = exit_code;
#else
	result.exit_code = WEXITSTATUS(exit_code);
#endif // windows
	return result;
}

#ifdef _WIN32

static process_result run_process_without_capture(std::string command, output_kind output)
{
	assert(output != output_kind::null_);

	// taken from https://docs.microsoft.com/hu-hu/windows/win32/procthread/creating-processes?redirectedfrom=MSDN
	STARTUPINFO si;
	PROCESS_INFORMATION pi;

	ZeroMemory( &si, sizeof(si) );
	si.cb = sizeof(si);
	ZeroMemory( &pi, sizeof(pi) );

	if (output == output_kind::stderr_)
	{
		si.hStdOutput = GetStdHandle(STD_ERROR_HANDLE);
	}

	// Start the child process.
	if( !CreateProcess(
		NULL,           // No module name (use command line)
		command.data(),
		NULL,           // Process handle not inheritable
		NULL,           // Thread handle not inheritable
		FALSE,          // Set handle inheritance to FALSE
		0,              // No creation flags
		NULL,           // Use parent's environment block
		NULL,           // Use parent's starting directory
		&si,            // Pointer to STARTUPINFO structure
		&pi )           // Pointer to PROCESS_INFORMATION structure
	)
	{
		fmt::print("CreateProcess failed");
		return { 0, 0, 1 };
	}

	// Wait until child process exits.
	WaitForSingleObject( pi.hProcess, INFINITE );

	DWORD exit_code;
	GetExitCodeProcess(pi.hProcess, &exit_code);

	// Close process and thread handles.
	CloseHandle( pi.hProcess );
	CloseHandle( pi.hThread );

	return { 0, 0, static_cast<int>(exit_code) };
}

#else

static process_result run_process_without_capture(std::string command, output_kind output)
{
	assert(output != output_kind::null_);
	if (output == output_kind::stderr_)
	{
		command += " 1>&2";
	}

	auto const exit_code = std::system(command.c_str());
	return { 0, 0, WEXITSTATUS(exit_code) };
}

#endif // windows

process_result run_command(std::string_view executable, cppb::vector<std::string> arguments, output_kind output)
{
	std::string command{ executable };
	for (auto const &arg : arguments)
	{
		command += ' ';
		command += arg;
	}

	if (output == output_kind::null_)
	{
		return run_process_with_capture(std::move(command), output);
	}
	else
	{
		return run_process_without_capture(std::move(command), output);
	}
}
