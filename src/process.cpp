#include "process.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#endif // windows

#ifdef _WIN32

struct handle_closer
{
	handle_closer(HANDLE h)
		: _h(h)
	{}

	~handle_closer(void)
	{
		if (this->_h != INVALID_HANDLE_VALUE)
		{
			CloseHandle(this->_h);
		}
	}

	handle_closer(handle_closer const &other) = delete;
	handle_closer(handle_closer &&other) = delete;
	handle_closer &operator = (handle_closer const &rhs) = delete;
	handle_closer &operator = (handle_closer &&rhs) = delete;

	void reset(void)
	{
		if (this->_h != INVALID_HANDLE_VALUE)
		{
			CloseHandle(this->_h);
			this->_h = INVALID_HANDLE_VALUE;
		}
	}

private:
	HANDLE _h;
};

struct attribute_list_freer
{
	attribute_list_freer(LPPROC_THREAD_ATTRIBUTE_LIST attribute_list)
		: _attribute_list(attribute_list)
	{}

	~attribute_list_freer(void)
	{
		if (this->_attribute_list != nullptr)
		{
			HeapFree(GetProcessHeap(), 0, this->_attribute_list);
		}
	}

	attribute_list_freer(attribute_list_freer const &other) = delete;
	attribute_list_freer(attribute_list_freer &&other) = delete;
	attribute_list_freer &operator = (attribute_list_freer const &rhs) = delete;
	attribute_list_freer &operator = (attribute_list_freer &&rhs) = delete;

	void reset(void)
	{
		if (this->_attribute_list != nullptr)
		{
			HeapFree(GetProcessHeap(), 0, this->_attribute_list);
			this->_attribute_list = nullptr;
		}
	}

private:
	LPPROC_THREAD_ATTRIBUTE_LIST _attribute_list;
};

struct attribute_list_deleter
{
	attribute_list_deleter(LPPROC_THREAD_ATTRIBUTE_LIST attribute_list)
		: _attribute_list(attribute_list)
	{}

	~attribute_list_deleter(void)
	{
		if (this->_attribute_list != nullptr)
		{
			DeleteProcThreadAttributeList(this->_attribute_list);
		}
	}

	attribute_list_deleter(attribute_list_deleter const &other) = delete;
	attribute_list_deleter(attribute_list_deleter &&other) = delete;
	attribute_list_deleter &operator = (attribute_list_deleter const &rhs) = delete;
	attribute_list_deleter &operator = (attribute_list_deleter &&rhs) = delete;

	void reset(void)
	{
		if (this->_attribute_list != nullptr)
		{
			DeleteProcThreadAttributeList(this->_attribute_list);
			this->_attribute_list = nullptr;
		}
	}

private:
	LPPROC_THREAD_ATTRIBUTE_LIST _attribute_list;
};

// https://stackoverflow.com/a/46348112/11488457
static process_result run_process(std::string_view command_line, bool capture)
{
	auto result = process_result();

	SECURITY_ATTRIBUTES  security_attributes = {
		.nLength = sizeof (SECURITY_ATTRIBUTES),
		.lpSecurityDescriptor = nullptr,
		.bInheritHandle = TRUE,
	};

	HANDLE stdout_reader = INVALID_HANDLE_VALUE;
	HANDLE stdout_writer = INVALID_HANDLE_VALUE;
	if (capture && !CreatePipe(&stdout_reader, &stdout_writer, &security_attributes, 0))
	{
		result.exit_code = -1;
		return result;
	}
	auto stdout_reader_closer = handle_closer(stdout_reader);
	auto stdout_writer_closer = handle_closer(stdout_writer);

	HANDLE stderr_reader = INVALID_HANDLE_VALUE;
	HANDLE stderr_writer = INVALID_HANDLE_VALUE;
	if (capture && !CreatePipe(&stderr_reader, &stderr_writer, &security_attributes, 0))
	{
		result.exit_code = -1;
		return result;
	}
	auto stderr_reader_closer = handle_closer(stderr_reader);
	auto stderr_writer_closer = handle_closer(stderr_writer);

	// Make a copy because CreateProcess needs to modify string buffer
	auto command_line_str = std::string(command_line);

	PROCESS_INFORMATION process_info;
	ZeroMemory(&process_info, sizeof (PROCESS_INFORMATION));

	WINBOOL success = TRUE;
	if (stdout_writer == INVALID_HANDLE_VALUE)
	{
		STARTUPINFO startup_info;
		ZeroMemory(&startup_info, sizeof (STARTUPINFO));
		startup_info.cb = sizeof (STARTUPINFO);

		success = CreateProcess(
			nullptr,
			command_line_str.data(),
			nullptr,
			nullptr,
			FALSE,
			0,
			nullptr,
			nullptr,
			&startup_info,
			&process_info
		);
	}
	else
	{
		// https://devblogs.microsoft.com/oldnewthing/20111216-00/?p=8873
		LPPROC_THREAD_ATTRIBUTE_LIST attribute_list = nullptr;
		auto const list_freer = attribute_list_freer(attribute_list);
		SIZE_T size = 0;
		success = InitializeProcThreadAttributeList(nullptr, 1, 0, &size) || GetLastError() == ERROR_INSUFFICIENT_BUFFER;

		if (success)
		{
			attribute_list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(HeapAlloc(GetProcessHeap(), 0, size));
			success = attribute_list != nullptr;
		}

		if (success)
		{
			success = InitializeProcThreadAttributeList(attribute_list, 1, 0, &size);
		}

		auto const list_deleter = attribute_list_deleter(success ? attribute_list : nullptr);
		HANDLE handles_to_inherit[] = {
			stdout_writer,
			stderr_writer,
		};
		if (success)
		{
			success = UpdateProcThreadAttribute(
				attribute_list,
				0,
				PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
				handles_to_inherit,
				2 * sizeof (HANDLE),
				nullptr,
				nullptr
			);
		}

		if (success)
		{
			STARTUPINFOEX startup_info = {
				.StartupInfo = {
					.cb = sizeof (STARTUPINFO),
					.hStdInput = 0,
					.hStdOutput = stdout_writer,
					.hStdError = stderr_writer,
				},
				.lpAttributeList = attribute_list,
			};

			startup_info.StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
			success = CreateProcess(
				nullptr,
				command_line_str.data(),
				nullptr,
				nullptr,
				TRUE,
				EXTENDED_STARTUPINFO_PRESENT,
				nullptr,
				nullptr,
				&startup_info.StartupInfo,
				&process_info
			);
		}
	}
	auto process_closer = handle_closer(process_info.hProcess);
	auto thread_closer = handle_closer(process_info.hThread);

	stdout_writer_closer.reset();
	stderr_writer_closer.reset();

	if(!success)
	{
		result.exit_code = -1;
		return result;
	}

	thread_closer.reset();

	if (capture && (stdout_reader || stderr_reader))
	{
		std::array<char, 1024> buffer = {};
		WINBOOL stdout_success = TRUE;
		WINBOOL stderr_success = TRUE;
		while (stdout_success || stderr_success)
		{
			if (stdout_reader)
			{
				DWORD read_size = 0;
				stdout_success = ReadFile(
					stdout_reader,
					buffer.data(),
					static_cast<DWORD>(buffer.size()),
					&read_size,
					nullptr
				);
				stdout_success = stdout_success && read_size != 0;
				if (stdout_success)
				{
					result.stdout_string += std::string_view(buffer.data(), read_size);
				}
			}

			if (stderr_reader)
			{
				DWORD read_size = 0;
				stderr_success = ReadFile(
					stderr_reader,
					buffer.data(),
					static_cast<DWORD>(buffer.size()),
					&read_size,
					nullptr
				);
				stderr_success = stderr_success && read_size != 0;
				if (stderr_success)
				{
					result.stderr_string += std::string_view(buffer.data(), read_size);
				}
			}
		}
	}

	WaitForSingleObject(process_info.hProcess, INFINITE);
	DWORD exit_code = 0;
	if(GetExitCodeProcess(process_info.hProcess, &exit_code))
	{
		result.exit_code = static_cast<int>(exit_code);
	}
	else
	{
		result.exit_code = -1;
	}

	if (capture)
	{
		for (
			auto it = result.stderr_string.find("error:");
			it != std::string::npos;
			it = result.stderr_string.find("error:", it + 1)
		)
		{
			result.error_count += 1;
		}
		for (
			auto it = result.stderr_string.find("warning:");
			it != std::string::npos;
			it = result.stderr_string.find("warning:", it + 1)
		)
		{
			result.warning_count += 1;
		}
	}

	return result;
}

static void write_escaped_string(std::string &buffer, std::string_view str)
{
	auto const needs_escaping = ranges::basic_range(str).is_any([](auto const c) {
		return c == ' ' || c == '\t' || c == '\"';
	});
	if (needs_escaping)
	{
		buffer += '\"';

		auto it = str.begin();
		while (it != str.end())
		{
			auto const next_it_pos = str.find(it, '\"');
			auto const it_pos = static_cast<size_t>(it - str.begin());
			auto const s = str.substr(it_pos, next_it_pos - it_pos);
			buffer += s;
			// '\"' needs to escaped as '\\\"'
			if (next_it_pos != std::string_view::npos)
			{
				if (s.ends_with('\\'))
				{
					// this is '\\"'
					buffer += "\\\\\"";
				}
				else
				{
					buffer += "\\\"";
				}
				it = str.begin() + next_it_pos + 1;
			}
			else
			{
				break;
			}
		}

		if (buffer.ends_with('\\'))
		{
			buffer += "\\\"";
		}
		else
		{
			buffer += '\"';
		}
	}
	else
	{
		buffer += str;
	}
}

#else

struct pipe_closer
{
	pipe_closer(int pipe_id)
		: _pipe_id(pipe_id),
		  _closed(false)
	{}

	~pipe_closer(void)
	{
		if (!this->_closed)
		{
			close(this->_pipe_id);
		}
	}

	pipe_closer(pipe_closer const &other) = delete;
	pipe_closer(pipe_closer &&other) = delete;
	pipe_closer &operator = (pipe_closer const &rhs) = delete;
	pipe_closer &operator = (pipe_closer &&rhs) = delete;

	void reset(void)
	{
		close(this->_pipe_id);
		this->_closed = true;
	}

private:
	int _pipe_id;
	bool _closed;
};

static process_result run_process(std::string_view command_line, bool capture)
{
	auto result = process_result();

	static constexpr size_t PIPE_READ = 0;
	static constexpr size_t PIPE_WRITE = 1;

	int stdout_pipe[2];
	int stderr_pipe[2];

	if (capture && pipe(stdout_pipe) < 0)
	{
		result.exit_code = -1;
		return result;
	}
	auto stdout_read_closer = pipe_closer(stdout_pipe[PIPE_READ]);
	auto stdout_write_closer = pipe_closer(stdout_pipe[PIPE_WRITE]);

	if (capture && pipe(stderr_pipe) < 0)
	{
		result.exit_code = -1;
		return result;
	}
	auto stderr_read_closer = pipe_closer(stderr_pipe[PIPE_READ]);
	auto stderr_write_closer = pipe_closer(stderr_pipe[PIPE_WRITE]);

	auto command_line_str = std::string(command_line);
	char bin_sh[] = "/bin/sh";
	char c[] = "-c";

	char *argv[] = {
		bin_sh,
		c,
		command_line_str.data(),
		nullptr
	};

	int id = fork();
	if (id == 0)
	{
		// child continues here

		// redirect stdout
		if (capture && dup2(stdout_pipe[PIPE_WRITE], STDOUT_FILENO) == -1)
		{
			exit(errno);
		}
		stdout_write_closer.reset();
		stdout_read_closer.reset();

		// redirect stderr
		if (capture && dup2(stderr_pipe[PIPE_WRITE], STDERR_FILENO) == -1)
		{
			exit(errno);
		}
		stderr_write_closer.reset();
		stderr_read_closer.reset();

		// run child process image
		// replace this with any exec* function find easier to use ("man exec")
		auto const execv_result = execv(bin_sh, argv);

		perror("execv");
		// if we get here at all, an error occurred, but we are in the child
		// process, so just exit
		exit(execv_result);
	}
	else if (id > 0)
	{
		// parent continues here

		// close unused file descriptors, these are for child only
		stdout_write_closer.reset();
		stderr_write_closer.reset();

		if (capture)
		{
			std::array<char, 1024> buffer = {};
			while (true)
			{
				// read stdout
				auto const stdout_read_size = read(stdout_pipe[PIPE_READ], buffer.data(), buffer.size());
				if (stdout_read_size != 0)
				{
					result.stdout_string += std::string_view(buffer.data(), static_cast<size_t>(stdout_read_size));
				}

				// read stderr
				auto const stderr_read_size = read(stderr_pipe[PIPE_READ], buffer.data(), buffer.size());
				if (stderr_read_size != 0)
				{
					result.stderr_string += std::string_view(buffer.data(), static_cast<size_t>(stderr_read_size));
				}

				if (stdout_read_size == 0 && stderr_read_size == 0)
				{
					break;
				}
			}
		}

		int status;
		if (waitpid(id, &status, 0) < 0)
		{
			result.exit_code = -1;
		}
		else
		{
			result.exit_code = status;
		}
	}
	else
	{
		// failed to create child
		result.exit_code = -1;
	}

	if (capture)
	{
		for (
			auto it = result.stderr_string.find("error:");
			it != std::string::npos;
			it = result.stderr_string.find("error:", it + 1)
		)
		{
			result.error_count += 1;
		}
		for (
			auto it = result.stderr_string.find("warning:");
			it != std::string::npos;
			it = result.stderr_string.find("warning:", it + 1)
		)
		{
			result.warning_count += 1;
		}
	}

	return result;
}

// https://stackoverflow.com/a/20053121/11488457
static void write_escaped_string(std::string &buffer, std::string_view str)
{
	auto const needs_escaping = !ranges::basic_range(str).is_all([](auto const c) {
		return (c >= 'a' && c <= 'z')
			|| (c >= 'A' && c <= 'Z')
			|| (c >= '0' && c <= '9')
			|| c == ','
			|| c == '.'
			|| c == '_'
			|| c == '+'
			|| c == ':'
			|| c == '@'
			|| c == '%'
			|| c == '/'
			|| c == '-';
	});
	if (!needs_escaping)
	{
		buffer += str;
		return;
	}

	buffer += '\'';

	auto it = str.begin();
	while (it != str.end())
	{
		auto const next_it_pos = str.find(it, '\'');
		auto const it_pos = static_cast<size_t>(it - str.begin());
		auto const s = str.substr(it_pos, next_it_pos - it_pos);
		buffer += s;
		if (next_it_pos != std::string_view::npos)
		{
			// this is "'\''"
			buffer += "\'\\\'\'";
			it = str.begin() + next_it_pos + 1;
		}
		else
		{
			break;
		}
	}

	buffer += '\'';
}

#endif // windows

std::string make_command_string(std::string_view command, std::span<std::string const> args)
{
	std::string command_string = "";

	write_escaped_string(command_string, command);
	for (auto const &arg : std::forward<decltype(args)>(args))
	{
		command_string += ' ';
		write_escaped_string(command_string, arg);
	}

	return command_string;
}

process_result run_command(std::string_view command, bool capture)
{
	return run_process(command, capture);
}

process_result run_command(std::string_view executable, cppb::vector<std::string> const &arguments, bool capture)
{
	return run_command(make_command_string(executable, arguments), capture);
}

std::pair<std::string, bool> capture_command_output(std::string_view executable, cppb::vector<std::string> const &arguments)
{
	auto const process_result = run_process(make_command_string(executable, arguments), true);
	return { process_result.stdout_string + process_result.stderr_string, process_result.exit_code == 0 };
}
