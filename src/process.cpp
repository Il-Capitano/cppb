#include "process.h"
#include <cassert>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#endif // windows


#ifdef _WIN32

static int run_process(std::string command, output_kind output)
{
	// from https://docs.microsoft.com/windows/win32/procthread/creating-processes
	STARTUPINFO si;
	PROCESS_INFORMATION pi;

	ZeroMemory( &si, sizeof(si) );
	si.cb = sizeof(si);
	ZeroMemory( &pi, sizeof(pi) );
	HANDLE null_handle = nullptr;

	if (output == output_kind::stdout_)
	{
		// nothing
	}
	else if (output == output_kind::stderr_)
	{
		si.dwFlags |= STARTF_USESTDHANDLES;
		si.hStdOutput = GetStdHandle(STD_ERROR_HANDLE);
		si.hStdError = si.hStdOutput;
	}
	else if (output == output_kind::null_)
	{
		si.dwFlags |= STARTF_USESTDHANDLES;
		null_handle = CreateFile(
			"nul",
			GENERIC_WRITE,
			FILE_SHARE_WRITE | FILE_SHARE_DELETE | FILE_SHARE_READ,
			nullptr,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL,
			nullptr
		);
		assert(null_handle != INVALID_HANDLE_VALUE);
		si.hStdOutput = null_handle;
		si.hStdError = null_handle;
	}

	// Start the child process.
	if (!CreateProcess(
		nullptr,        // No module name (use command line)
		command.data(), // Command line
		nullptr,        // Process handle not inheritable
		nullptr,        // Thread handle not inheritable
		FALSE,          // Set handle inheritance to FALSE
		0,              // No creation flags
		nullptr,        // Use parent's environment block
		nullptr,        // Use parent's starting directory
		&si,            // Pointer to STARTUPINFO structure
		&pi             // Pointer to PROCESS_INFORMATION structure
	))
	{
		fmt::print("CreateProcess failed ({}).\n", GetLastError());
		return -1;
	}

	// Wait until child process exits.
	WaitForSingleObject( pi.hProcess, INFINITE );

	DWORD exit_code;
	GetExitCodeProcess(pi.hProcess, &exit_code);

	// Close process and thread handles.
	CloseHandle( pi.hProcess );
	CloseHandle( pi.hThread );

	if (null_handle != nullptr)
	{
		CloseHandle(null_handle);
	}

	return static_cast<int>(exit_code);
}

#else

static int run_process(std::string command, output_kind output)
{
	if (output == output_kind::stdout_)
	{
		// nothing
	}
	else if (output == output_kind::stderr_)
	{
		// not implemented
	}
	else if (output == output_kind::null_)
	{
		command += " > /dev/null 2>&1";
	}

	return std::system(command.c_str());
}

#endif // windows

int run_command(std::string_view executable, cppb::vector<std::string> arguments, output_kind output)
{
	std::string command{ executable };
	for (auto const &arg : arguments)
	{
		command += ' ';
		command += arg;
	}
	
	return run_process(std::move(command), output);
}
