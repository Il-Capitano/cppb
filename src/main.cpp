#include <cstdio>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <thread>
#include <mutex>
#include <utility>
#include <span>
#include <fmt/color.h>
#include "core.h"
#include "analyze.h"
#include "config.h"
#include "process.h"
#include "cl_options.h"

#ifdef _WIN32
#include <windows.h>
#endif // windows

#ifdef _WIN32

static void report_error(std::string_view site, std::string_view message)
{
	auto const h = GetStdHandle(STD_ERROR_HANDLE);
	auto const original_attributes = [h]() {
		CONSOLE_SCREEN_BUFFER_INFO info;
		GetConsoleScreenBufferInfo(h, &info);
		return info.wAttributes;
	}();

	constexpr WORD foreground_bits = FOREGROUND_INTENSITY | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
	constexpr WORD background_bits = BACKGROUND_INTENSITY | BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE;
	constexpr WORD other_bits = static_cast<WORD>(~(foreground_bits | background_bits));

	WORD const original_background = original_attributes & background_bits;
	WORD const original_other      = original_attributes & other_bits;

	constexpr WORD bright_white = FOREGROUND_INTENSITY | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
	constexpr WORD bright_red = FOREGROUND_INTENSITY | FOREGROUND_RED;

	SetConsoleTextAttribute(h, bright_white | original_background | original_other);
	fmt::print(stderr, "{}: ", site);
	SetConsoleTextAttribute(h, bright_red | original_background | original_other);
	fmt::print(stderr, "error: ");
	SetConsoleTextAttribute(h, original_attributes);
	fmt::print(stderr, "{}\n", message);
}

static void report_warning(std::string_view site, std::string_view message)
{
	auto const h = GetStdHandle(STD_ERROR_HANDLE);
	auto const original_attributes = [h]() {
		CONSOLE_SCREEN_BUFFER_INFO info;
		GetConsoleScreenBufferInfo(h, &info);
		return info.wAttributes;
	}();

	constexpr WORD foreground_bits = FOREGROUND_INTENSITY | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
	constexpr WORD background_bits = BACKGROUND_INTENSITY | BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE;
	constexpr WORD other_bits = static_cast<WORD>(~(foreground_bits | background_bits));

	WORD const original_background = original_attributes & background_bits;
	WORD const original_other      = original_attributes & other_bits;

	constexpr WORD bright_white = FOREGROUND_INTENSITY | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
	constexpr WORD bright_magenta = FOREGROUND_INTENSITY | FOREGROUND_RED | FOREGROUND_BLUE;

	SetConsoleTextAttribute(h, bright_white | original_background | original_other);
	fmt::print(stderr, "{}: ", site);
	SetConsoleTextAttribute(h, bright_magenta | original_background | original_other);
	fmt::print(stderr, "warning: ");
	SetConsoleTextAttribute(h, original_attributes);
	fmt::print(stderr, "{}\n", message);
}

#else

static void report_error(std::string_view site, std::string_view message)
{
	fmt::print(stderr, fg(fmt::terminal_color::bright_white), "{}: ", site);
	fmt::print(stderr, fg(fmt::terminal_color::bright_red), "error: ");
	fmt::print(stderr, "{}\n", message);
}

static void report_warning(std::string_view site, std::string_view message)
{
	fmt::print(stderr, fg(fmt::terminal_color::bright_white), "{}: ", site);
	fmt::print(stderr, fg(fmt::terminal_color::bright_magenta), "warning: ");
	fmt::print(stderr, "{}\n", message);
}

#endif // windows

namespace os
{

#ifdef _WIN32

constexpr std::string_view executable_extension = ".exe";

static std::string_view config_name(void)
{
	if (ctcli::option_value<"build --build-mode"> == build_mode::debug)
	{
		return "windows-debug";
	}
	else
	{
		return "windows-release";
	}
}

static config const &get_build_config(project_config const &project_config)
{
	if (ctcli::option_value<"build --build-mode"> == build_mode::debug)
	{
		return project_config.windows_debug;
	}
	else
	{
		return project_config.windows_release;
	}
}

#else

constexpr std::string_view executable_extension = "";

static std::string_view config_name(void)
{
	if (ctcli::option_value<"build --build-mode"> == build_mode::debug)
	{
		return "linux-debug";
	}
	else
	{
		return "linux-release";
	}
}

static config const &get_build_config(project_config const &project_config)
{
	if (ctcli::option_value<"build --build-mode"> == build_mode::debug)
	{
		return project_config.linux_debug;
	}
	else
	{
		return project_config.linux_release;
	}
}

#endif // windows

} // namespace os

static uint64_t get_job_count(void)
{
	auto const result = ctcli::is_option_set<"build --jobs">()
		? ctcli::option_value<"build --jobs">
		: std::thread::hardware_concurrency();
	return std::max(result, uint64_t(1));
}

struct run_rule_result_t
{
	int exit_code;
	bool any_run;
	fs::file_time_type last_update_time;
};

static run_rule_result_t run_rule(
	std::string_view point_name,  // pre-build, pre-link, post-build
	std::string_view rule_to_run,
	cppb::vector<rule> const &rules,
	fs::file_time_type config_last_update,
	std::string &error,
	bool error_on_unknown_rule = true
)
{
	auto const it = std::find_if(rules.begin(), rules.end(), [rule_to_run](auto const &rule) {
		return rule_to_run == rule.rule_name
			|| (rule.is_file && fs::absolute(fs::path(rule_to_run)) == fs::absolute(fs::path(rule.rule_name)));
	});
	auto const rule_path = fs::path(rule_to_run);
	if (it == rules.end())
	{
		if (error_on_unknown_rule && !fs::exists(rule_path))
		{
			error = fmt::format("'{}' is not a rule name or a file", rule_to_run);
			return {};
		}
		return { 0, false, fs::last_write_time(rule_path) };
	}

	auto const last_rule_write_time = fs::exists(rule_path) ? fs::last_write_time(rule_path) : fs::file_time_type::min();
	run_rule_result_t result = {
		0, false,
		it->is_file ? last_rule_write_time : fs::file_time_type::min()
	};
	for (auto const &dependency : it->dependencies)
	{
		auto const [exit_code, any_run, last_update_time] = run_rule("", dependency, rules, config_last_update, error, error_on_unknown_rule);
		if (!error.empty())
		{
			return {};
		}
		result.any_run |= any_run;
		result.last_update_time = std::max(result.last_update_time, last_update_time);
		if (exit_code != 0)
		{
			result.exit_code = exit_code;
			return result;
		}
	}

	if (
		!it->is_file
		|| !fs::exists(rule_path)
		|| last_rule_write_time < std::max(config_last_update, result.last_update_time)
	)
	{
		result.any_run = true;
		for (auto const &command : it->commands)
		{
			if (point_name.empty())
			{
				fmt::print("running rule '{}': {}\n", rule_to_run, command);
			}
			else
			{
				fmt::print("running {} rule '{}': {}\n", point_name, rule_to_run, command);
			}
			std::fflush(stdout);
			auto const &[_0, _1, exit_code, _2] = run_command(command, output_kind::stderr_);
			if (exit_code != 0)
			{
				result.exit_code = exit_code;
				return result;
			}
		}
		if (it->is_file && fs::exists(rule_path))
		{
			result.last_update_time = fs::last_write_time(rule_path);
		}
	}
	return result;
}

static run_rule_result_t run_rules(
	std::string_view point_name,  // pre-build, pre-link, post-build, link dependency
	cppb::vector<std::string> const &rules_to_run,
	cppb::vector<rule> const &rules,
	fs::file_time_type config_last_update,
	std::string &error,
	bool error_on_unknown_rule = true
)
{
	bool any_rules_run = false;
	fs::file_time_type last_update_time = config_last_update;
	for (auto const &rule_to_run : rules_to_run)
	{
		auto const [exit_code, any_run, last_update] = run_rule(point_name, rule_to_run, rules, config_last_update, error, error_on_unknown_rule);
		any_rules_run |= any_run;
		last_update_time = std::max(last_update_time, last_update);
		if (exit_code != 0 || !error.empty())
		{
			return { exit_code, any_rules_run, last_update_time };
		}
	}
	return { 0, any_rules_run, last_update_time };
}

static run_rule_result_t run_rules(
	std::string_view point_name,  // pre-build, pre-link, post-build, link dependency
	cppb::vector<fs::path> const &rules_to_run,
	cppb::vector<rule> const &rules,
	fs::file_time_type config_last_update,
	std::string &error,
	bool error_on_unknown_rule = true
)
{
	bool any_rules_run = false;
	fs::file_time_type last_update_time = config_last_update;
	for (auto const &rule_to_run : rules_to_run)
	{
		auto const [exit_code, any_run, last_update] = run_rule(point_name, rule_to_run.generic_string(), rules, config_last_update, error, error_on_unknown_rule);
		any_rules_run |= any_run;
		last_update_time = std::max(last_update_time, last_update);
		if (exit_code != 0 || !error.empty())
		{
			return { exit_code, any_rules_run, last_update_time };
		}
	}
	return { 0, any_rules_run, last_update_time };
}

static void print_command(std::string_view executable, cppb::vector<std::string> const &arguments)
{
	std::string command{ executable };
	for (auto const &arg : arguments)
	{
		command += ' ';
		command += arg;
	}
	fmt::print("{}\n", command);
	std::fflush(stdout);
}

static cppb::vector<std::string> get_common_c_compiler_flags(config const &build_config)
{
	cppb::vector<std::string> result;

	result.emplace_back("-c");
	if (ctcli::option_value<"build --build-mode"> == build_mode::debug)
	{
		result.emplace_back("-g");
	}
	add_c_compiler_flags(result, build_config);
	for (auto const positional_arg : ctcli::positional_arguments<ctcli::command("build")>)
	{
		result.emplace_back(positional_arg);
	}

	return result;
}

static cppb::vector<std::string> get_common_cpp_compiler_flags(config const &build_config)
{
	cppb::vector<std::string> result;

	result.emplace_back("-c");
	if (ctcli::option_value<"build --build-mode"> == build_mode::debug)
	{
		result.emplace_back("-g");
	}
	add_cpp_compiler_flags(result, build_config);
	for (auto const positional_arg : ctcli::positional_arguments<ctcli::command("build")>)
	{
		result.emplace_back(positional_arg);
	}

	return result;
}

static std::string get_c_compiler(config const &build_config)
{
	if (!build_config.c_compiler_path.empty())
	{
		return build_config.c_compiler_path.string();
	}
	else
	{
		switch (build_config.compiler)
		{
		case compiler_kind::gcc:
			if (build_config.compiler_version == -1)
			{
				return "gcc";
			}
			else
			{
				return fmt::format("gcc-{}", build_config.compiler_version);
			}
		case compiler_kind::clang:
			if (build_config.compiler_version == -1)
			{
				return "clang";
			}
			else
			{
				return fmt::format("clang-{}", build_config.compiler_version);
			}
		}
	}
}

static std::string get_cpp_compiler(config const &build_config)
{
	if (!build_config.cpp_compiler_path.empty())
	{
		return build_config.cpp_compiler_path.string();
	}
	else
	{
		switch (build_config.compiler)
		{
		case compiler_kind::gcc:
			if (build_config.compiler_version == -1)
			{
				return "g++";
			}
			else
			{
				return fmt::format("g++-{}", build_config.compiler_version);
			}
		case compiler_kind::clang:
			if (build_config.compiler_version == -1)
			{
				return "clang++";
			}
			else
			{
				return fmt::format("clang++-{}", build_config.compiler_version);
			}
		}
	}
}

struct build_result_t
{
	int exit_code;
	bool any_run;
	bool any_cpp;
	cppb::vector<fs::path> object_files;
};

static int link_project(
	std::string_view project_name,
	config const &build_config,
	fs::path const &bin_directory,
	cppb::vector<fs::path> const &object_files,
	fs::file_time_type dependency_last_update,
	bool is_any_cpp
)
{
	auto const c_compiler   = get_c_compiler(build_config);
	auto const cpp_compiler = get_cpp_compiler(build_config);

	auto const project_directory_name = fs::current_path().filename().generic_string();
	auto const executable_file_name = project_name == "default"
		? fmt::format("{}{}", project_directory_name, os::executable_extension)
		: fmt::format("{}-{}{}", project_name, project_directory_name, os::executable_extension);

	auto const executable_file = fs::absolute(bin_directory / executable_file_name);
	auto const last_object_write_time = object_files
		.transform([](auto const &object_file) { return fs::last_write_time(object_file); })
		.max(dependency_last_update);

	if (
		ctcli::option_value<"build --link">
		|| !fs::exists(executable_file)
		|| fs::last_write_time(executable_file) < last_object_write_time
	)
	{
		auto const relative_executable_file_name = fs::relative(executable_file).generic_string();
		cppb::vector<std::string> link_args;
		link_args.emplace_back("-o");
		link_args.emplace_back(executable_file.generic_string());
		for (auto const &object_file : object_files)
		{
			link_args.emplace_back(object_file.generic_string());
		}
		for (auto const &link_dependency : build_config.link_dependencies)
		{
			link_args.emplace_back(link_dependency.generic_string());
		}
		add_link_flags(link_args, build_config);

		fmt::print("linking {}\n", relative_executable_file_name);
		std::fflush(stdout);
		if (ctcli::option_value<"build -v">)
		{
			print_command(is_any_cpp ? cpp_compiler : c_compiler, link_args);
		}
		auto const result = run_command(is_any_cpp ? cpp_compiler : c_compiler, link_args, output_kind::stderr_);
		if (result.exit_code != 0)
		{
			return result.exit_code;
		}
	}

	return 0;
}

struct compiler_invocation_t
{
	std::string compiler;
	cppb::vector<std::string> args;
	fs::path input_file;
	fs::file_time_type input_file_last_modified;
	fs::path output_file;
};

struct compilation_info_t
{
	std::size_t index;
	std::string filename;
};

static cppb::vector<process_result> run_commands_async(std::span<compiler_invocation_t const> compiler_invocations)
{
	auto const invocation_count = compiler_invocations.size();

	auto const job_count = std::min(get_job_count(), invocation_count);
	assert(job_count > 1);

	auto const invocation_mutexes_ptr = std::make_unique<std::mutex[]>(invocation_count);
	auto const invocation_mutexes = std::span(invocation_mutexes_ptr.get(), invocation_count);
	// initialize invocation_mutexes to locked; they will be unlocked once the invocation returns
	for (auto &m : invocation_mutexes)
	{
		m.lock();
	}

	cppb::vector<process_result> compilation_results;
	compilation_results.resize(invocation_count);

	auto get_next_compiler_invocation = [
		&compiler_invocations,
		current_index = std::size_t(0),
		mutex = std::mutex()
	]() mutable -> std::optional<std::size_t> {
		auto const guard = std::lock_guard(mutex);

		if (current_index < compiler_invocations.size())
		{
			auto const result_index = current_index;
			current_index += 1;
			return result_index;
		}
		else
		{
			return std::nullopt;
		}
	};

	auto thread_pool = cppb::vector<std::jthread>();

	for (std::size_t i = 0; i < job_count; ++i)
	{
		thread_pool.push_back(std::jthread([&]() {
			while (auto const invocation_index = get_next_compiler_invocation())
			{
				auto const index = *invocation_index;
				auto const &invocation = compiler_invocations[index];
				compilation_results[index] = run_command(invocation.compiler, invocation.args, output_kind::capture);
				// unlock the mutex at the given index to signal that the process has finished
				invocation_mutexes[index].unlock();
			}
		}));
	}

	int const index_width = [&]() {
		auto i = compiler_invocations.size();
		int result = 0;
		do
		{
			++result;
			i /= 10;
		} while (i != 0);
		return result;
	}();

	for (std::size_t i = 0; i < invocation_count; ++i)
	{
		auto const filename = fs::relative(compiler_invocations[i].input_file).generic_string();
		fmt::print("({:{}}/{}) {}\n", i + 1, index_width, compiler_invocations.size(), filename);
		std::fflush(stdout);
		if (ctcli::option_value<"build --verbose">)
		{
			print_command(compiler_invocations[i].compiler, compiler_invocations[i].args);
		}

		// block until the process has finished running
		invocation_mutexes[i].lock();
		invocation_mutexes[i].unlock();

		// print the output of the compiler, if there is any
		auto const &output = compilation_results[i].captured_output;
		if (output != "")
		{
			if (output.ends_with('\n'))
			{
				fmt::print("{}", output);
			}
			else
			{
				fmt::print("{}\n", output);
			}
		}
	}

	return compilation_results;
}

static int run_commands_sequential(std::span<compiler_invocation_t const> compiler_invocations)
{
	int const index_width = [&]() {
		auto i = compiler_invocations.size();
		int result = 0;
		do
		{
			++result;
			i /= 10;
		} while (i != 0);
		return result;
	}();

	for (std::size_t i = 0; i < compiler_invocations.size(); ++i)
	{
		auto const &invocation = compiler_invocations[i];
		auto const filename = fs::relative(invocation.input_file).generic_string();
		fmt::print("({:{}}/{}) {}\n", i + 1, index_width, compiler_invocations.size(), filename);
		std::fflush(stdout);
		if (ctcli::option_value<"build --verbose">)
		{
			print_command(invocation.compiler, invocation.args);
		}

		auto const result = run_command(invocation.compiler, invocation.args, output_kind::stderr_);
		if (result.exit_code != 0)
		{
			return result.exit_code;
		}
	}

	return 0;
}

struct project_compiler_invocations_t
{
	std::optional<compiler_invocation_t> c_pch;
	std::optional<compiler_invocation_t> cpp_pch;
	cppb::vector<compiler_invocation_t> translation_units;
	bool is_any_c;
	bool is_any_cpp;
};

static std::optional<compiler_invocation_t> get_pch_compiler_invocation(
	config const &build_config,
	cppb::vector<source_file> const &source_files,
	fs::path const &intermediate_bin_directory,
	fs::path const &header_file,
	std::string_view compiler,
	cppb::vector<std::string> &compiler_args,
	std::string_view header_type // c-header or c++-header
)
{
	auto const header_it = std::find_if(
		source_files.begin(), source_files.end(),
		[&](auto const &source) {
			return fs::equivalent(source.file_path, header_file);
		}
	);
	if (header_it == source_files.end())
	{
		report_error(
			header_file.generic_string(),
			fmt::format("header file '{}' doesn't exist or it was never included", header_file.generic_string())
		);
		return std::nullopt;
	}
	auto const pch_file = [&]() {
		switch (build_config.compiler)
		{
		case compiler_kind::gcc:
		{
			auto file = header_file;
			file += ".gch";
			return file;
		}
		case compiler_kind::clang:
		{
			auto file = intermediate_bin_directory / header_file.filename();
			file += ".pch";
			return file;
		}
		}
	}();

	auto const compiler_args_size = compiler_args.size();

	compiler_args.emplace_back("-o");
	compiler_args.emplace_back(pch_file.generic_string());
	compiler_args.emplace_back("-x");
	compiler_args.emplace_back(header_type);
	compiler_args.emplace_back(header_file.generic_string());

	auto result = compiler_invocation_t{
		.compiler = std::string(compiler),
		.args = compiler_args,
		.input_file = header_it->file_path,
		.input_file_last_modified = header_it->last_modified_time,
		.output_file = pch_file,
	};

	compiler_args.resize(compiler_args_size);

	switch (build_config.compiler)
	{
	case compiler_kind::gcc:
		break;
	case compiler_kind::clang:
		compiler_args.emplace_back("-include-pch");
		compiler_args.emplace_back(pch_file.generic_string());
		break;
	}

	return result;
}

static std::optional<project_compiler_invocations_t> get_compiler_invocations(
	config const &build_config,
	cppb::vector<source_file> const &source_files,
	fs::path const &intermediate_bin_directory
)
{
	project_compiler_invocations_t result;

	auto const c_compiler   = get_c_compiler(build_config);
	auto const cpp_compiler = get_cpp_compiler(build_config);

	cppb::vector<std::string> c_compiler_args   = get_common_c_compiler_flags(build_config);
	cppb::vector<std::string> cpp_compiler_args = get_common_cpp_compiler_flags(build_config);

	if (!build_config.c_precompiled_header.empty())
	{
		result.c_pch = get_pch_compiler_invocation(
			build_config,
			source_files,
			intermediate_bin_directory,
			build_config.c_precompiled_header,
			c_compiler,
			c_compiler_args,
			"c-header"
		);

		if (!result.c_pch.has_value())
		{
			return std::nullopt;
		}
	}
	if (!build_config.cpp_precompiled_header.empty())
	{
		result.cpp_pch = get_pch_compiler_invocation(
			build_config,
			source_files,
			intermediate_bin_directory,
			build_config.cpp_precompiled_header,
			cpp_compiler,
			cpp_compiler_args,
			"c++-header"
		);

		if (!result.cpp_pch.has_value())
		{
			return std::nullopt;
		}
	}

	auto const compilation_units = source_files.filter(
		[source_directory = fs::absolute(build_config.source_directory).lexically_normal()](auto const &source) {
			auto const source_size     = std::distance(source.file_path.begin(), source.file_path.end());
			auto const source_dir_size = std::distance(source_directory.begin(), source_directory.end());
			auto const is_in_source_directory = source_size > source_dir_size
				&& std::equal(source_directory.begin(), source_directory.end(), source.file_path.begin());
			return is_in_source_directory && source_extensions.is_any([&source](auto const extension) {
				return source.file_path.extension().generic_string() == extension;
			});
		}
	).collect<cppb::vector>();

	cppb::vector<compile_command> compile_commands;

	auto const emit_compile_commands = (
			ctcli::option_value<"build --build-mode"> == build_mode::debug
			&& (ctcli::option_value<"build --emit-compile-commands"> || build_config.emit_compile_commands)
		) && [&]() {
			fs::path const compile_commands_json = "./compile_commands.json";
			return !fs::exists(compile_commands_json)
				|| fs::last_write_time(compile_commands_json) < source_files
					.transform([](auto const &source) { return source.last_modified_time; })
					.max(fs::file_time_type::min());
		}();

	if (emit_compile_commands)
	{
		compile_commands.reserve(compilation_units.size());
	}

	// source file compilation
	for (std::size_t i = 0; i < compilation_units.size(); ++i)
	{
		auto const &source = compilation_units[i];
		auto const &source_file = source.file_path;
		auto const is_c_source = source_file.extension() == ".c";
		result.is_any_c |= is_c_source;
		result.is_any_cpp |= !is_c_source;

		auto object_file = intermediate_bin_directory / fs::relative(source_file, build_config.source_directory);
		fs::create_directories(object_file.parent_path());
		object_file += ".o";
		auto const source_file_name = source_file.generic_string();

		auto &args = is_c_source ? c_compiler_args : cpp_compiler_args;
		auto const args_old_size = args.size();
		args.emplace_back("-o");
		args.emplace_back(object_file.generic_string());
		args.emplace_back(source_file_name);

		result.translation_units.push_back(compiler_invocation_t{
			.compiler = std::string(is_c_source ? c_compiler : cpp_compiler),
			.args = args,
			.input_file = source_file,
			.input_file_last_modified = source.last_modified_time,
			.output_file = std::move(object_file),
		});

		if (emit_compile_commands)
		{
			compile_commands.push_back({ std::move(source_file_name), args });
		}

		args.resize(args_old_size);
	}

	if (compile_commands.size() != 0)
	{
		compile_commands.sort([](auto const &lhs, auto const &rhs) { return lhs.source_file < rhs.source_file; });
		write_compile_commands_json(compile_commands);
	}

	return std::move(result);
}

static build_result_t build_project(
	config const &build_config,
	cppb::vector<source_file> const &source_files,
	fs::path const &intermediate_bin_directory,
	fs::file_time_type dependency_last_update
)
{
	auto const invocations = get_compiler_invocations(build_config, source_files, intermediate_bin_directory);
	if (!invocations.has_value())
	{
		return {
			.exit_code = 1,
			.any_run = false,
			.any_cpp = false,
			.object_files = {},
		};
	}

	auto c_pch_last_update   = fs::file_time_type::min();
	auto cpp_pch_last_update = fs::file_time_type::min();

	if (invocations->c_pch.has_value())
	{
		auto const header_last_modified = invocations->c_pch->input_file_last_modified;
		auto const &pch_file = invocations->c_pch->output_file;
		auto const pch_last_write_time = fs::exists(pch_file) ? fs::last_write_time(pch_file) : fs::file_time_type::min();
		if (
			ctcli::option_value<"build --rebuild">
			|| pch_last_write_time < header_last_modified
			|| pch_last_write_time < dependency_last_update
		)
		{
			auto const relative_header_filename = fs::relative(invocations->c_pch->input_file).generic_string();
			fmt::print("pre-compiling {}\n", relative_header_filename);
			std::fflush(stdout);
			if (ctcli::option_value<"build --verbose">)
			{
				print_command(invocations->c_pch->compiler, invocations->c_pch->args);
			}
			auto const result = run_command(invocations->c_pch->compiler, invocations->c_pch->args, output_kind::stderr_);
			if (result.exit_code != 0)
			{
				return { result.exit_code, false, false, {} };
			}
		}
		if (fs::exists(pch_file))
		{
			c_pch_last_update = fs::last_write_time(pch_file);
		}
	}
	if (invocations->cpp_pch.has_value())
	{
		auto const header_last_modified = invocations->cpp_pch->input_file_last_modified;
		auto const &pch_file = invocations->cpp_pch->output_file;
		auto const pch_last_write_time = fs::exists(pch_file) ? fs::last_write_time(pch_file) : fs::file_time_type::min();
		if (
			ctcli::option_value<"build --rebuild">
			|| pch_last_write_time < header_last_modified
			|| pch_last_write_time < dependency_last_update
		)
		{
			auto const relative_header_filename = fs::relative(invocations->cpp_pch->input_file).generic_string();
			fmt::print("pre-compiling {}\n", relative_header_filename);
			std::fflush(stdout);
			if (ctcli::option_value<"build --verbose">)
			{
				print_command(invocations->cpp_pch->compiler, invocations->cpp_pch->args);
			}
			auto const result = run_command(invocations->cpp_pch->compiler, invocations->cpp_pch->args, output_kind::stderr_);
			if (result.exit_code != 0)
			{
				return { result.exit_code, false, false, {} };
			}
		}
		if (fs::exists(pch_file))
		{
			cpp_pch_last_update = fs::last_write_time(pch_file);
		}
	}

	auto const compiler_invocations = invocations->translation_units
		.filter([&](compiler_invocation_t const &invocation) {
			auto const object_last_write_time = fs::exists(invocation.output_file)
				? fs::last_write_time(invocation.output_file)
				: fs::file_time_type::min();
			auto const is_c_source = invocation.input_file.extension() == ".c";
			return ctcli::option_value<"build --rebuild">
				|| object_last_write_time < invocation.input_file_last_modified
				|| object_last_write_time < dependency_last_update
				|| object_last_write_time < (is_c_source ? c_pch_last_update : cpp_pch_last_update);
		})
		.collect<cppb::vector>();

	auto object_files = invocations->translation_units
		.transform([](compiler_invocation_t const &invocation) {
			return invocation.output_file;
		})
		.collect<cppb::vector>();

	if (compiler_invocations.empty())
	{
		return { 0, false, invocations->is_any_cpp, std::move(object_files) };
	}

	auto const job_count = get_job_count();

	if (!ctcli::option_value<"build -s"> && job_count > 1)
	{
		cppb::vector<process_result> compilation_results = run_commands_async(compiler_invocations);

		bool is_good = true;
		assert(compilation_results.size() == compiler_invocations.size());
		for (std::size_t i = 0; i < compilation_results.size(); ++i)
		{
			auto const result = compilation_results[i];
			if (result.exit_code != 0 || result.error_count != 0 || result.warning_count != 0)
			{
				is_good = is_good && result.exit_code == 0 && result.error_count == 0;
				auto const message = [&]() {
					if (result.error_count != 0 && result.warning_count != 0)
					{
						return fmt::format(
							"compilation failed with {} error{} and {} warning{}",
							result.error_count, result.error_count == 1 ? "" : "s",
							result.warning_count, result.warning_count == 1 ? "" : "s"
						);
					}
					else if (result.error_count != 0)
					{
						return fmt::format(
							"compilation failed with {} error{}",
							result.error_count, result.error_count == 1 ? "" : "s"
						);
					}
					else if (result.warning_count != 0)
					{
						return fmt::format(
							"{} warning{} emitted by compiler",
							result.warning_count, result.warning_count == 1 ? "" : "s"
						);
					}
					else // if (result.error_count == 0 && result.warning_cont == 0)
					{
						// this shouldn't normally happen, but we handle it anyways
						return fmt::format("compilation failed with exit code {}", result.exit_code);
					}
				}();
				if (result.exit_code != 0 || result.error_count != 0)
				{
					auto const relative_source_file_name = fs::relative(compiler_invocations[i].input_file).generic_string();
					report_error(relative_source_file_name, message);
				}
				else
				{
					auto const relative_source_file_name = fs::relative(compiler_invocations[i].input_file).generic_string();
					report_warning(relative_source_file_name, message);
				}
			}
		}

		if (!is_good)
		{
			return { 1, false, false, {} };
		}

		return { 0, true, invocations->is_any_cpp, std::move(object_files) };
	}
	else
	{
		auto const exit_code = run_commands_sequential(compiler_invocations);
		return { exit_code, true, invocations->is_any_cpp, std::move(object_files) };
	}
}

static int build_project(project_config const &project_config, cppb::vector<rule> const &rules, fs::file_time_type config_last_update)
{
	std::string error;

	auto const &build_config = os::get_build_config(project_config);

	auto const bin_directory = fs::path(ctcli::option_value<"build --bin-dir">) / os::config_name();
	auto const intermediate_bin_directory = bin_directory / fmt::format("int-{}", project_config.project_name);
	fs::create_directories(intermediate_bin_directory);

	auto const cppb_dir = fs::path(ctcli::option_value<"build --cppb-dir">);
	auto const dependency_file_path = cppb_dir / fmt::format("dependencies/{}.json", os::config_name());
	auto source_files = read_dependency_json(dependency_file_path, error);
	if (!error.empty())
	{
		report_error(dependency_file_path.generic_string(), error);
		return 1;
	}

	// pre-build rules
	auto const [prebuild_exit_code, prebuild_any_run, prebuild_last_update] = run_rules("pre-build", build_config.prebuild_rules, rules, config_last_update, error);
	if (!error.empty())
	{
		report_error("cppb", error);
		return 1;
	}
	if (prebuild_exit_code != 0)
	{
		return prebuild_exit_code;
	}

	fill_last_modified_times(source_files);
	analyze_source_files(
		get_source_files_in_directory(build_config.source_directory),
		build_config.include_paths,
		source_files,
		fs::exists(dependency_file_path)
			? fs::last_write_time(dependency_file_path)
			: fs::file_time_type::min(),
		config_last_update
	);
	source_files.sort([](source_file const &lhs, source_file const &rhs) {
		auto lhs_it = lhs.file_path.begin();
		auto rhs_it = rhs.file_path.begin();
		auto const lhs_end = lhs.file_path.end();
		auto const rhs_end = rhs.file_path.end();

		while (lhs_it != lhs_end && rhs_it != rhs_end)
		{
			auto const compare_result = *lhs_it <=> *rhs_it;

			++lhs_it;
			++rhs_it;

			if (lhs_it == lhs_end || rhs_it == rhs_end)
			{
				return lhs_it != lhs_end || (rhs_it == rhs_end && compare_result < 0);
			}
			else if (compare_result < 0)
			{
				return true;
			}
			else if (compare_result > 0)
			{
				return false;
			}
		}

		// we should never get here...
		return lhs_it != lhs_end;
	});
	write_dependency_json(dependency_file_path, source_files);

	auto const build_dependency_last_update = std::max(config_last_update, prebuild_last_update);

	auto [exit_code, any_run, any_cpp, object_files] = build_project(
		build_config,
		source_files,
		intermediate_bin_directory,
		build_dependency_last_update
	);
	if (exit_code != 0)
	{
		return exit_code;
	}

	// pre-link rules
	auto const [prelink_exit_code, prelink_any_run, prelink_last_update] = run_rules("pre-link", build_config.prelink_rules, rules, config_last_update, error);
	if (!error.empty())
	{
		report_error("cppb", error);
		return 1;
	}
	if (prelink_exit_code != 0)
	{
		return prelink_exit_code;
	}

	auto const [link_dep_exit_code, link_dep_any_run, link_dep_last_update] = run_rules("link dependency", build_config.link_dependencies, rules, config_last_update, error, false);
	if (!error.empty())
	{
		report_error("cppb", error);
		return 1;
	}
	if (link_dep_exit_code != 0)
	{
		return link_dep_exit_code;
	}

	auto const link_dependency_last_update = std::max({ config_last_update, prelink_last_update, link_dep_last_update });

	auto const link_exit_code = link_project(project_config.project_name, build_config, bin_directory, object_files, link_dependency_last_update, any_cpp);
	if (link_exit_code != 0)
	{
		return link_exit_code;
	}

	// post-build rules
	auto const [postbuild_exit_code, postbuild_any_run, postbuild_last_update] = run_rules("post-build", build_config.postbuild_rules, rules, config_last_update, error);
	if (!error.empty())
	{
		report_error("cppb", error);
		return 1;
	}
	if (postbuild_exit_code != 0)
	{
		return postbuild_exit_code;
	}

	return 0;
}

static int run_project(project_config const &project_config)
{
	std::string error;

	auto const &build_config = os::get_build_config(project_config);

	auto const bin_directory = fs::path(ctcli::option_value<"build --bin-dir">) / os::config_name();

	auto const project_directory_name = fs::current_path().filename().generic_string();
	auto const executable_file_name = project_config.project_name == "default"
		? fmt::format("{}{}", project_directory_name, os::executable_extension)
		: fmt::format("{}-{}{}", project_config.project_name, project_directory_name, os::executable_extension);

	auto const executable_file = fs::absolute(bin_directory / executable_file_name);

	std::string run_info = fmt::format("running {}", fs::relative(executable_file).generic_string());
	for (auto const &arg : build_config.run_args)
	{
		run_info += ' ';
		run_info += arg;
	}
	fmt::print("{}\n", run_info);
	std::fflush(stdout);
	return run_command(executable_file.string(), build_config.run_args, output_kind::stdout_).exit_code;
}

static int build_command(void)
{
	std::string error;

	auto const config_file_path = fs::path(ctcli::option_value<"build --config-file">);
	auto const [project_configs, rules] = read_config_json(config_file_path, error);
	if (!error.empty())
	{
		report_error(config_file_path.generic_string(), error);
		return 1;
	}

	auto const &project_config = [&project_configs = project_configs]() -> auto & {
		std::string_view const config_to_build = ctcli::option_value<"build --build-config">;
		auto const it = std::find_if(
			project_configs.begin(), project_configs.end(),
			[config_to_build](auto const &config) {
				return config_to_build == config.project_name;
			}
		);
		if (it == project_configs.end())
		{
			report_error(
				fmt::format("<command-line>:{}", ctcli::option_index<"build --build-config">),
				fmt::format("unknown configuration '{}'", config_to_build)
			);
			exit(1);
		}
		return *it;
	}();
	return build_project(project_config, rules, fs::last_write_time(config_file_path));
}

static int run_command(void)
{
	std::string error;

	auto const config_file_path = fs::path(ctcli::option_value<"build --config-file">);
	auto const [project_configs, rules] = read_config_json(config_file_path, error);
	if (!error.empty())
	{
		report_error(config_file_path.generic_string(), error);
		return 1;
	}

	auto const &project_config = [&project_configs = project_configs]() -> auto & {
		std::string_view const config_to_build = ctcli::option_value<"build --build-config">;
		auto const it = std::find_if(
			project_configs.begin(), project_configs.end(),
			[config_to_build](auto const &config) {
				return config_to_build == config.project_name;
			}
		);
		if (it == project_configs.end())
		{
			report_error(
				fmt::format("<command-line>:{}", ctcli::option_index<"build --build-config">),
				fmt::format("unknown configuration '{}'", config_to_build)
			);
			exit(1);
		}
		return *it;
	}();
	auto const build_result = build_project(project_config, rules, fs::last_write_time(config_file_path));
	if (build_result != 0)
	{
		return build_result;
	}
	return run_project(project_config);
}

static int run_rule_command(void)
{
	std::string error;

	auto const config_file_path = fs::path(ctcli::option_value<"run-rule --config-file">);
	auto const [project_configs, rules] = read_config_json(config_file_path, error);
	if (!error.empty())
	{
		report_error(config_file_path.generic_string(), error);
		return 1;
	}

	auto const rule_to_run = ctcli::command_value<"run-rule">;
	auto const last_modified_time = ctcli::option_value<"run-rule --force"> ? fs::file_time_type::max() : fs::file_time_type::min();
	auto const [exit_code, _0, _1] = run_rule("", rule_to_run, rules, last_modified_time, error);

	if (!error.empty())
	{
		report_error("cppb", error);
		return 1;
	}
	return exit_code;
}

static int new_command(void)
{
	auto const project_directory = fs::path(ctcli::command_value<"new">);
	auto const src_directory   = project_directory / ctcli::option_value<"new --src-dir">;
	auto const config_file     = project_directory / ctcli::option_value<"new --config-file">;

	fs::create_directories(src_directory);
	fs::create_directories(config_file.parent_path());

	output_default_config_json(config_file, ctcli::option_value<"new --src-dir">);

	auto const default_main_path = src_directory / "main.cpp";
	if (!fs::exists(default_main_path))
	{
		std::ofstream default_main(default_main_path);
		default_main <<
R"(#include <iostream>

int main(void)
{
	std::cout << "Hello world!\n";
	return 0;
}
)";
	}
	return 0;
}

int main(int argc, char const **argv)
{
	if (argc == 1)
	{
		ctcli::print_commands_help<ctcli::commands_id_t::def>("cppb", 2, 24, 80);
		return 0;
	}
	auto const errors = ctcli::parse_command_line(argc, argv);
	if (!errors.empty())
	{
		for (auto const &error : errors)
		{
			report_error(fmt::format("<command-line>:{}", error.flag_position), error.message);
		}
		return 1;
	}

	if (ctcli::print_help_if_needed("cppb", 2, 24, 80))
	{
		return 0;
	}

	if (ctcli::is_command_set<"build">())
	{
		return build_command();
	}
	else if (ctcli::is_command_set<"run">())
	{
		return run_command();
	}
	else if (ctcli::is_command_set<"run-rule">())
	{
		return run_rule_command();
	}
	else if (ctcli::is_command_set<"new">())
	{
		return new_command();
	}

	return 0;
}
