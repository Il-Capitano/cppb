#include <cstdio>
#include <chrono>
#include <future>
#include <fstream>
#include <filesystem>
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
	std::string &error
)
{
	auto const it = std::find_if(rules.begin(), rules.end(), [rule_to_run](auto const &rule) {
		return rule_to_run == rule.rule_name;
	});
	auto const rule_path = fs::path(rule_to_run);
	if (it == rules.end())
	{
		if (!fs::exists(rule_path))
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
		auto const [exit_code, any_run, last_update_time] = run_rule("", dependency, rules, config_last_update, error);
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
			fmt::print(
				point_name.empty() ? "running {}rule '{}': {}\n" : "running {} rule '{}': {}\n",
				point_name, rule_to_run, command
			);
			auto const [_, __, exit_code] = run_command(command, output_kind::stderr_);
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
	std::string_view point_name,  // pre-build, pre-link, post-build
	cppb::vector<std::string> const &rules_to_run,
	cppb::vector<rule> const &rules,
	fs::file_time_type config_last_update,
	std::string &error
)
{
	bool any_rules_run = false;
	fs::file_time_type last_update_time = config_last_update;
	for (auto const &rule_to_run : rules_to_run)
	{
		auto const [exit_code, any_run, last_update] = run_rule(point_name, rule_to_run, rules, config_last_update, error);
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
	auto const executable_file_name = [&]() -> std::string {
#ifdef _WIN32
		if (project_name == "default")
		{
			return fmt::format("{}.exe", project_directory_name);
		}
		else
		{
			return fmt::format("{}-{}.exe", project_name, project_directory_name);
		}
#else
		if (project_name == "default")
		{
			return project_directory_name;
		}
		else
		{
			return fmt::format("{}-{}", project_name, project_directory_name);
		}
#endif // windows
	}();

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
		fmt::print("linking {}\n", relative_executable_file_name);
		cppb::vector<std::string> link_args;
		link_args.emplace_back("-o");
		link_args.emplace_back(executable_file.generic_string());
		for (auto const &object_file : object_files)
		{
			link_args.emplace_back(object_file.generic_string());
		}
		add_link_flags(link_args, build_config);

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

static build_result_t build_project_async(
	config const &build_config,
	cppb::vector<source_file> const &source_files,
	fs::path const &intermediate_bin_directory,
	fs::file_time_type dependency_last_update
)
{
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
	cppb::vector<compile_command> compile_commands;

	cppb::vector<fs::path> object_files;
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

	int const compilation_units_count_width = [&]() {
		auto i = compilation_units.size();
		int result = 0;
		do
		{
			++result;
			i /= 10;
		} while (i != 0);
		return result;
	}();

	if (emit_compile_commands)
	{
		compile_commands.reserve(compilation_units.size());
	}
	cppb::vector<process_result> compilation_results;
	cppb::vector<std::pair<std::size_t, std::future<process_result>>> compilation_futures;
	auto const is_any_future_finished = [&compilation_futures]() {
		for (auto const &[index, future] : compilation_futures)
		{
			using namespace std::chrono_literals;
			if (future.wait_for(0ms) == std::future_status::ready)
			{
				return true;
			}
		}
		return false;
	};

	auto const get_finished_future = [&compilation_futures]() {
		using namespace std::chrono_literals;
		return std::find_if(
			compilation_futures.begin(), compilation_futures.end(),
			[](auto const &index_future) {
				return index_future.second.wait_for(0ms) == std::future_status::ready;
			}
		);
	};

	compilation_results.resize(compilation_units.size());

	auto const c_compiler   = get_c_compiler(build_config);
	auto const cpp_compiler = get_cpp_compiler(build_config);

	auto common_c_flags   = get_common_c_compiler_flags(build_config);
	auto common_cpp_flags = get_common_cpp_compiler_flags(build_config);

	cppb::vector<std::string> c_compiler_args   = common_c_flags;
	cppb::vector<std::string> cpp_compiler_args = common_cpp_flags;

	auto c_pch_last_update   = fs::file_time_type::min();
	auto cpp_pch_last_update = fs::file_time_type::min();

	if (!build_config.c_precompiled_header.empty())
	{
		auto const &header_file = build_config.c_precompiled_header;
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
			return { 1, false, false, {} };
		}
		auto const pch_file = [&]() {
			switch (build_config.compiler)
			{
			case compiler_kind::gcc:
			{
				auto result = header_file;
				result += ".gch";
				return result;
			}
			case compiler_kind::clang:
			{
				auto result = intermediate_bin_directory / header_file.filename();
				result += ".pch";
				return result;
			}
			}
		}();

		auto const pch_last_write_time = fs::exists(pch_file) ? fs::last_write_time(pch_file) : fs::file_time_type::min();
		c_pch_last_update = pch_last_write_time;
		if (
			ctcli::option_value<"build --rebuild">
			|| pch_last_write_time < header_it->last_modified_time
			|| pch_last_write_time < dependency_last_update
		)
		{
			auto const header_file_name = header_file.generic_string();
			auto const pch_file_name = pch_file.generic_string();

			c_compiler_args.emplace_back("-o");
			c_compiler_args.emplace_back(pch_file_name);
			c_compiler_args.emplace_back("-x");
			c_compiler_args.emplace_back("c-header");
			c_compiler_args.emplace_back(header_file_name);

			auto const relative_header_file_name = fs::relative(header_file).generic_string();

			fmt::print("pre-compiling {}\n", relative_header_file_name);
			if (ctcli::option_value<"build --verbose">)
			{
				print_command(c_compiler, c_compiler_args);
			}
			auto const result = run_command(c_compiler, c_compiler_args, output_kind::stderr_);
			if (result.exit_code != 0)
			{
				return { result.exit_code, false, false, {} };
			}
			if (fs::exists(pch_file))
			{
				c_pch_last_update = fs::last_write_time(pch_file);
			}

			c_compiler_args.resize(common_c_flags.size());
		}
		switch (build_config.compiler)
		{
		case compiler_kind::gcc:
			break;
		case compiler_kind::clang:
			common_c_flags.emplace_back("-include-pch");
			common_c_flags.emplace_back(pch_file.generic_string());
			c_compiler_args.emplace_back("-include-pch");
			c_compiler_args.emplace_back(pch_file.generic_string());
			break;
		}
	}
	if (!build_config.cpp_precompiled_header.empty())
	{
		auto const &header_file = build_config.cpp_precompiled_header;
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
			return { 1, false, false, {} };
		}
		auto const pch_file = [&]() {
			switch (build_config.compiler)
			{
			case compiler_kind::gcc:
			{
				auto result = header_file;
				result += ".gch";
				return result;
			}
			case compiler_kind::clang:
			{
				auto result = intermediate_bin_directory / header_file.filename();
				result += ".pch";
				return result;
			}
			}
		}();

		auto const pch_last_write_time = fs::exists(pch_file) ? fs::last_write_time(pch_file) : fs::file_time_type::min();
		cpp_pch_last_update = pch_last_write_time;
		if (
			ctcli::option_value<"build --rebuild">
			|| pch_last_write_time < header_it->last_modified_time
			|| pch_last_write_time < dependency_last_update
		)
		{
			auto const header_file_name = header_file.generic_string();
			auto const pch_file_name = pch_file.generic_string();

			cpp_compiler_args.emplace_back("-o");
			cpp_compiler_args.emplace_back(pch_file_name);
			cpp_compiler_args.emplace_back("-x");
			cpp_compiler_args.emplace_back("c++-header");
			cpp_compiler_args.emplace_back(header_file_name);

			auto const relative_header_file_name = fs::relative(header_file).generic_string();

			fmt::print("pre-compiling {}\n", relative_header_file_name);
			if (ctcli::option_value<"build --verbose">)
			{
				print_command(cpp_compiler, cpp_compiler_args);
			}
			auto const result = run_command(cpp_compiler, cpp_compiler_args, output_kind::stderr_);
			if (result.exit_code != 0)
			{
				return { result.exit_code, false, false, {} };
			}
			if (fs::exists(pch_file))
			{
				cpp_pch_last_update = fs::last_write_time(pch_file);
			}

			cpp_compiler_args.resize(common_cpp_flags.size());
		}
		switch (build_config.compiler)
		{
		case compiler_kind::gcc:
			break;
		case compiler_kind::clang:
			common_cpp_flags.emplace_back("-include-pch");
			common_cpp_flags.emplace_back(pch_file.generic_string());
			cpp_compiler_args.emplace_back("-include-pch");
			cpp_compiler_args.emplace_back(pch_file.generic_string());
			break;
		}
	}

	auto const job_count = ctcli::option_value<"build --jobs">;
	bool is_any_cpp = false;
	bool any_run = false;
	// source file compilation
	for (std::size_t i = 0; i < compilation_units.size(); ++i)
	{
		auto const &source = compilation_units[i];
		auto const &source_file = source.file_path;
		auto const is_c_source = source_file.extension() == ".c";
		if (!is_c_source)
		{
			is_any_cpp = true;
		}
		object_files.emplace_back(intermediate_bin_directory / source_file.filename());
		object_files.back() += ".o";
		auto const &object_file = object_files.back();
		auto const source_file_name = source_file.generic_string();

		auto &args = is_c_source ? c_compiler_args : cpp_compiler_args;
		args.emplace_back("-o");
		args.emplace_back(object_file.generic_string());
		args.emplace_back(source_file_name);

		if (
			auto const object_last_write_time = fs::exists(object_file) ? fs::last_write_time(object_file) : fs::file_time_type::min();
			ctcli::option_value<"build --rebuild">
			|| object_last_write_time < source.last_modified_time
			|| object_last_write_time < dependency_last_update
			|| object_last_write_time < (is_c_source ? c_pch_last_update : cpp_pch_last_update)
		)
		{
			any_run = true;
			auto const relative_source_file_name = fs::relative(source_file).generic_string();

			if (emit_compile_commands)
			{
				compile_commands.push_back({ source_file_name, args });
			}

			if (compilation_futures.size() == job_count)
			{
				while (!is_any_future_finished())
				{
					using namespace std::chrono_literals;
					compilation_futures.front().second.wait_for(20ms);
				}
				auto const finished = get_finished_future();
				assert(finished != compilation_futures.end());
				compilation_results[finished->first] = finished->second.get();
				compilation_futures.erase(finished);
			}
			fmt::print("({:{}}/{}) {}\n", i + 1, compilation_units_count_width, compilation_units.size(), relative_source_file_name);
			if (ctcli::option_value<"build --verbose">)
			{
				print_command(is_c_source ? c_compiler : cpp_compiler, args);
			}
			compilation_futures.push_back({
				i,
				std::async(
					static_cast<process_result (*)(std::string_view, cppb::vector<std::string> const &, output_kind)>(&run_command),
					is_c_source ? c_compiler : cpp_compiler, args, output_kind::null_
				)
			});
		}
		else if (emit_compile_commands)
		{
			compile_commands.push_back({ std::move(source_file_name), args });
		}

		if (is_c_source)
		{
			args.resize(common_c_flags.size());
		}
		else
		{
			args.resize(common_cpp_flags.size());
		}
	}

	for (auto &[index, future] : compilation_futures)
	{
		compilation_results[index] = future.get();
	}

	bool is_good = true;
	assert(compilation_results.size() == compilation_units.size());
	for (std::size_t i = 0; i < compilation_results.size(); ++i)
	{
		auto const result = compilation_results[i];
		auto const relative_source_file_name = fs::relative(compilation_units[i].file_path).generic_string();
		if (result.exit_code != 0 || result.error_count != 0 || result.warning_count != 0)
		{
			is_good = is_good && result.exit_code == 0 && result.error_count == 0;
			auto const message = [&]() {
				if (result.error_count != 0 && result.warning_count != 0)
				{
					return fmt::format(
						"compilation failed with {} error{} and {} warning{}; use '-s' to see compiler output",
						result.error_count, result.error_count == 1 ? "" : "s",
						result.warning_count, result.warning_count == 1 ? "" : "s"
					);
				}
				else if (result.error_count != 0)
				{
					return fmt::format(
						"compilation failed with {} error{}; use '-s' to see compiler output",
						result.error_count, result.error_count == 1 ? "" : "s"
					);
				}
				else if (result.warning_count != 0)
				{
					return fmt::format(
						"{} warning{} emitted by compiler; use '-s' to see compiler output",
						result.warning_count, result.warning_count == 1 ? "" : "s"
					);
				}
				else // if (result.error_count == 0 && result.warning_cont == 0)
				{
					// this shouldn't normally happen, but we handle it anyways
					return fmt::format("compilation failed with exit code {}; use '-s' to see compiler output", result.exit_code);
				}
			}();
			if (result.exit_code != 0 || result.error_count != 0)
			{
				report_error(relative_source_file_name, message);
			}
			else
			{
				report_warning(relative_source_file_name, message);
			}
		}
	}

	if (!is_good)
	{
		return { 1, false, false, {} };
	}

	if (compile_commands.size() != 0)
	{
		compile_commands.sort([](auto const &lhs, auto const &rhs) { return lhs.source_file < rhs.source_file; });
		write_compile_commands_json(compile_commands);
	}

	return { 0, any_run, is_any_cpp, std::move(object_files) };
}

static build_result_t build_project_sequential(
	config const &build_config,
	cppb::vector<source_file> const &source_files,
	fs::path const &intermediate_bin_directory,
	fs::file_time_type dependency_last_update
)
{
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
	cppb::vector<compile_command> compile_commands;

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

	int const compilation_units_count_width = [&]() {
		auto i = compilation_units.size();
		int result = 0;
		do
		{
			++result;
			i /= 10;
		} while (i != 0);
		return result;
	}();

	if (emit_compile_commands)
	{
		compile_commands.reserve(compilation_units.size());
	}
	cppb::vector<process_result> compilation_results;
	cppb::vector<std::pair<std::size_t, std::future<process_result>>> compilation_futures;

	compilation_results.resize(compilation_units.size());
	cppb::vector<fs::path> object_files;
	object_files.reserve(compilation_units.size());

	auto const c_compiler   = get_c_compiler(build_config);
	auto const cpp_compiler = get_cpp_compiler(build_config);

	auto common_c_flags   = get_common_c_compiler_flags(build_config);
	auto common_cpp_flags = get_common_cpp_compiler_flags(build_config);

	cppb::vector<std::string> c_compiler_args   = common_c_flags;
	cppb::vector<std::string> cpp_compiler_args = common_cpp_flags;

	auto c_pch_last_update   = fs::file_time_type::min();
	auto cpp_pch_last_update = fs::file_time_type::min();

	if (!build_config.c_precompiled_header.empty())
	{
		auto const &header_file = build_config.c_precompiled_header;
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
			return { 1, false, false, {} };
		}
		auto const pch_file = [&]() {
			switch (build_config.compiler)
			{
			case compiler_kind::gcc:
			{
				auto result = header_file;
				result += ".gch";
				return result;
			}
			case compiler_kind::clang:
			{
				auto result = intermediate_bin_directory / header_file.filename();
				result += ".pch";
				return result;
			}
			}
		}();

		auto const pch_last_write_time = fs::exists(pch_file) ? fs::last_write_time(pch_file) : fs::file_time_type::min();
		c_pch_last_update = pch_last_write_time;
		if (
			ctcli::option_value<"build --rebuild">
			|| pch_last_write_time < header_it->last_modified_time
			|| pch_last_write_time < dependency_last_update
		)
		{
			auto const header_file_name = header_file.generic_string();
			auto const pch_file_name = pch_file.generic_string();

			c_compiler_args.emplace_back("-o");
			c_compiler_args.emplace_back(pch_file_name);
			c_compiler_args.emplace_back("-x");
			c_compiler_args.emplace_back("c-header");
			c_compiler_args.emplace_back(header_file_name);

			auto const relative_header_file_name = fs::relative(header_file).generic_string();

			fmt::print("pre-compiling {}\n", relative_header_file_name);
			if (ctcli::option_value<"build --verbose">)
			{
				print_command(c_compiler, c_compiler_args);
			}
			auto const result = run_command(c_compiler, c_compiler_args, output_kind::stderr_);
			if (result.exit_code != 0)
			{
				return { result.exit_code, false, false, {} };
			}
			if (fs::exists(pch_file))
			{
				c_pch_last_update = fs::last_write_time(pch_file);
			}

			c_compiler_args.resize(common_c_flags.size());
		}
		switch (build_config.compiler)
		{
		case compiler_kind::gcc:
			break;
		case compiler_kind::clang:
			common_c_flags.emplace_back("-include-pch");
			common_c_flags.emplace_back(pch_file.generic_string());
			c_compiler_args.emplace_back("-include-pch");
			c_compiler_args.emplace_back(pch_file.generic_string());
			break;
		}
	}
	if (!build_config.cpp_precompiled_header.empty())
	{
		auto const &header_file = build_config.cpp_precompiled_header;
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
			return { 1, false, false, {} };
		}
		auto const pch_file = [&]() {
			switch (build_config.compiler)
			{
			case compiler_kind::gcc:
			{
				auto result = header_file;
				result += ".gch";
				return result;
			}
			case compiler_kind::clang:
			{
				auto result = intermediate_bin_directory / header_file.filename();
				result += ".pch";
				return result;
			}
			}
		}();

		auto const pch_last_write_time = fs::exists(pch_file) ? fs::last_write_time(pch_file) : fs::file_time_type::min();
		cpp_pch_last_update = pch_last_write_time;
		if (
			ctcli::option_value<"build --rebuild">
			|| pch_last_write_time < header_it->last_modified_time
			|| pch_last_write_time < dependency_last_update
		)
		{
			auto const header_file_name = header_file.generic_string();
			auto const pch_file_name = pch_file.generic_string();

			cpp_compiler_args.emplace_back("-o");
			cpp_compiler_args.emplace_back(pch_file_name);
			cpp_compiler_args.emplace_back("-x");
			cpp_compiler_args.emplace_back("c++-header");
			cpp_compiler_args.emplace_back(header_file_name);

			auto const relative_header_file_name = fs::relative(header_file).generic_string();

			fmt::print("pre-compiling {}\n", relative_header_file_name);
			if (ctcli::option_value<"build --verbose">)
			{
				print_command(cpp_compiler, cpp_compiler_args);
			}
			auto const result = run_command(cpp_compiler, cpp_compiler_args, output_kind::stderr_);
			if (result.exit_code != 0)
			{
				return { result.exit_code, false, false, {} };
			}
			if (fs::exists(pch_file))
			{
				cpp_pch_last_update = fs::last_write_time(pch_file);
			}

			cpp_compiler_args.resize(common_cpp_flags.size());
		}
		switch (build_config.compiler)
		{
		case compiler_kind::gcc:
			break;
		case compiler_kind::clang:
			common_cpp_flags.emplace_back("-include-pch");
			common_cpp_flags.emplace_back(pch_file.generic_string());
			cpp_compiler_args.emplace_back("-include-pch");
			cpp_compiler_args.emplace_back(pch_file.generic_string());
			break;
		}
	}

	bool is_any_cpp = false;
	bool any_run = false;
	// source file compilation
	for (std::size_t i = 0; i < compilation_units.size(); ++i)
	{
		auto const &source = compilation_units[i];
		auto const &source_file = source.file_path;
		auto const is_c_source = source_file.extension() == ".c";
		if (!is_c_source)
		{
			is_any_cpp = true;
		}
		object_files.emplace_back(intermediate_bin_directory / source_file.filename());
		object_files.back() += ".o";
		auto const &object_file = object_files.back();
		auto const source_file_name = source_file.generic_string();

		auto &args = is_c_source ? c_compiler_args : cpp_compiler_args;
		args.emplace_back("-o");
		args.emplace_back(object_file.generic_string());
		args.emplace_back(source_file_name);

		if (
			auto const object_last_write_time = fs::exists(object_file) ? fs::last_write_time(object_file) : fs::file_time_type::min();
			ctcli::option_value<"build --rebuild">
			|| object_last_write_time < source.last_modified_time
			|| object_last_write_time < dependency_last_update
			|| object_last_write_time < (is_c_source ? c_pch_last_update : cpp_pch_last_update)
		)
		{
			any_run = true;
			auto const relative_source_file_name = fs::relative(source_file).generic_string();

			if (emit_compile_commands)
			{
				compile_commands.push_back({ source_file_name, args });
			}

			fmt::print("({:{}}/{}) {}\n", i + 1, compilation_units_count_width, compilation_units.size(), relative_source_file_name);
			if (ctcli::option_value<"build --verbose">)
			{
				print_command(is_c_source ? c_compiler : cpp_compiler, args);
			}
			auto const result = run_command(is_c_source ? c_compiler : cpp_compiler, args, output_kind::stderr_);
			if (result.exit_code != 0)
			{
				return { result.exit_code, false, false, {} };
			}
		}
		else if (emit_compile_commands)
		{
			compile_commands.push_back({ std::move(source_file_name), args });
		}

		if (is_c_source)
		{
			args.resize(common_c_flags.size());
		}
		else
		{
			args.resize(common_cpp_flags.size());
		}
	}

	if (compile_commands.size() != 0)
	{
		compile_commands.sort([](auto const &lhs, auto const &rhs) { return lhs.source_file < rhs.source_file; });
		write_compile_commands_json(compile_commands);
	}

	return { 0, any_run, is_any_cpp, std::move(object_files) };
}

static int build_project(project_config const &project_config, cppb::vector<rule> const &rules, fs::file_time_type config_last_update)
{
	std::string error;

	auto const &build_config = [&]() -> auto & {
#ifdef _WIN32
		if (ctcli::option_value<"build --build-mode"> == build_mode::debug)
		{
			return project_config.windows_debug;
		}
		else
		{
			return project_config.windows_release;
		}
#else
		if (ctcli::option_value<"build --build-mode"> == build_mode::debug)
		{
			return project_config.linux_debug;
		}
		else
		{
			return project_config.linux_release;
		}
#endif // windows
	}();

	auto const bin_directory = [&]() {
#ifdef _WIN32
		if (ctcli::option_value<"build --build-mode"> == build_mode::debug)
		{
			return fs::path(ctcli::option_value<"build --bin-dir">) / "windows-debug";
		}
		else
		{
			return fs::path(ctcli::option_value<"build --bin-dir">) / "windows-release";
		}
#else
		if (ctcli::option_value<"build --build-mode"> == build_mode::debug)
		{
			return fs::path(ctcli::option_value<"build --bin-dir">) / "linux-debug";
		}
		else
		{
			return fs::path(ctcli::option_value<"build --bin-dir">) / "linux-release";
		}
#endif // windows
	}();
	auto const intermediate_bin_directory = bin_directory / fmt::format("int-{}", project_config.project_name);
	fs::create_directories(intermediate_bin_directory);

	auto const cppb_dir = fs::path(ctcli::option_value<"build --cppb-dir">);
	auto const dependency_file_path = [&]() {
#ifdef _WIN32
		if (ctcli::option_value<"build --build-mode"> == build_mode::debug)
		{
			return cppb_dir / "dependencies/windows-debug.json";
		}
		else
		{
			return cppb_dir / "dependencies/windows-release.json";
		}
#else
		if (ctcli::option_value<"build --build-mode"> == build_mode::debug)
		{
			return cppb_dir / "dependencies/linux-debug.json";
		}
		else
		{
			return cppb_dir / "dependencies/linux-release.json";
		}
#endif // windows
	}();
	auto source_files = read_dependency_json(dependency_file_path, error);
	if (!error.empty())
	{
		report_error(dependency_file_path.generic_string(), error);
		return 1;
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
	source_files.sort([](auto const &lhs, auto const &rhs) {
		auto const lhs_size = std::distance(lhs.file_path.begin(), lhs.file_path.end());
		auto const rhs_size = std::distance(rhs.file_path.begin(), rhs.file_path.end());
		return lhs_size > rhs_size || (lhs_size == rhs_size && lhs.file_path < rhs.file_path);
	});
	write_dependency_json(dependency_file_path, source_files);

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

	auto const build_dependency_last_update = std::max(config_last_update, prebuild_last_update);

	auto const job_count = ctcli::option_value<"build --jobs">;
	auto [exit_code, any_run, any_cpp, object_files] =
		(!ctcli::option_value<"build -s"> && job_count > 1)
			? build_project_async(
				build_config,
				source_files,
				intermediate_bin_directory,
				build_dependency_last_update
			)
			: build_project_sequential(
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

	auto const link_dependency_last_update = std::max(config_last_update, prelink_last_update);

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

	auto const &build_config = [&]() -> auto & {
#ifdef _WIN32
		if (ctcli::option_value<"build --build-mode"> == build_mode::debug)
		{
			return project_config.windows_debug;
		}
		else
		{
			return project_config.windows_release;
		}
#else
		if (ctcli::option_value<"build --build-mode"> == build_mode::debug)
		{
			return project_config.linux_debug;
		}
		else
		{
			return project_config.linux_release;
		}
#endif // windows
	}();

	auto const bin_directory = [&]() {
#ifdef _WIN32
		if (ctcli::option_value<"build --build-mode"> == build_mode::debug)
		{
			return fs::path(ctcli::option_value<"build --bin-dir">) / "windows-debug";
		}
		else
		{
			return fs::path(ctcli::option_value<"build --bin-dir">) / "windows-release";
		}
#else
		if (ctcli::option_value<"build --build-mode"> == build_mode::debug)
		{
			return fs::path(ctcli::option_value<"build --bin-dir">) / "linux-debug";
		}
		else
		{
			return fs::path(ctcli::option_value<"build --bin-dir">) / "linux-release";
		}
#endif // windows
	}();

	auto const project_directory_name = fs::current_path().filename().generic_string();
	auto const executable_file_name = [&]() -> std::string {
#ifdef _WIN32
		if (project_config.project_name == "default")
		{
			return fmt::format("{}.exe", project_directory_name);
		}
		else
		{
			return fmt::format("{}-{}.exe", project_config.project_name, project_directory_name);
		}
#else
		if (project_config.project_name == "default")
		{
			return project_directory_name;
		}
		else
		{
			return fmt::format("{}-{}", project_config.project_name, project_directory_name);
		}
#endif // windows
	}();

	auto const executable_file = fs::absolute(bin_directory / executable_file_name);

	std::string run_info = fmt::format("running {}", fs::relative(executable_file).generic_string());
	for (auto const &arg : build_config.run_args)
	{
		run_info += ' ';
		run_info += arg;
	}
	fmt::print("{}\n", run_info);
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
	auto const [exit_code, _, __] = run_rule("", rule_to_run, rules, last_modified_time, error);

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
