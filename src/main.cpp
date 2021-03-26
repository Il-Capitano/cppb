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

static bool ends_with(std::string_view str, std::string_view pattern)
{
	return ctcli::string_view(str).ends_with(pattern);
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

static cppb::vector<std::string> get_compiler_args(config const &build_config, std::string source_file_name, std::string object_file_name)
{
	cppb::vector<std::string> args;

	args.emplace_back("-c");
	if (ctcli::option_value<ctcli::option("build --build-mode")> == build_mode::debug)
	{
		args.emplace_back("-g");
	}
	if (ends_with(source_file_name, ".c"))
	{
		add_c_compiler_flags(args, build_config);
	}
	else
	{
		add_cpp_compiler_flags(args, build_config);
	}
	for (auto const positional_arg : ctcli::positional_arguments<ctcli::command("build")>)
	{
		args.emplace_back(positional_arg);
	}
	args.emplace_back("-o");
	args.emplace_back(std::move(object_file_name));
	args.emplace_back(std::move(source_file_name));

	return args;
}

static int build_project(project_config const &project_config, fs::file_time_type config_last_update)
{
	std::string error;

	auto const &build_config = [&]() -> auto & {
#ifdef _WIN32
		if (ctcli::option_value<ctcli::option("build --build-mode")> == build_mode::debug)
		{
			return project_config.windows_debug;
		}
		else
		{
			return project_config.windows_release;
		}
#else
		if (ctcli::option_value<ctcli::option("build --build-mode")> == build_mode::debug)
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
		if (ctcli::option_value<ctcli::option("build --build-mode")> == build_mode::debug)
		{
			return fs::path(ctcli::option_value<ctcli::option("build --bin-dir")>) / "windows-debug";
		}
		else
		{
			return fs::path(ctcli::option_value<ctcli::option("build --bin-dir")>) / "windows-release";
		}
#else
		if (ctcli::option_value<ctcli::option("build --build-mode")> == build_mode::debug)
		{
			return fs::path(ctcli::option_value<ctcli::option("build --bin-dir")>) / "linux-debug";
		}
		else
		{
			return fs::path(ctcli::option_value<ctcli::option("build --bin-dir")>) / "linux-release";
		}
#endif // windows
	}();
	auto const intermediate_bin_directory = bin_directory / fmt::format("int-{}", project_config.project_name);
	fs::create_directories(intermediate_bin_directory);

	auto const cppb_dir = fs::path(ctcli::option_value<ctcli::option("build --cppb-dir")>);
	auto const dependency_file_path = [&]() {
#ifdef _WIN32
		if (ctcli::option_value<ctcli::option("build --build-mode")> == build_mode::debug)
		{
			return cppb_dir / "dependencies/windows-debug.json";
		}
		else
		{
			return cppb_dir / "dependencies/windows-release.json";
		}
#else
		if (ctcli::option_value<ctcli::option("build --build-mode")> == build_mode::debug)
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

	auto const c_compiler = [&]() -> std::string {
		if (!build_config.c_compiler_path.empty())
		{
			return build_config.c_compiler_path.string();
		}
		else
		{
			switch (build_config.compiler)
			{
			case compiler_kind::gcc:
				return "gcc";
			case compiler_kind::clang:
				return "clang";
			}
		}
	}();

	auto const cpp_compiler = [&]() -> std::string {
		if (!build_config.cpp_compiler_path.empty())
		{
			return build_config.cpp_compiler_path.string();
		}
		else
		{
			switch (build_config.compiler)
			{
			case compiler_kind::gcc:
				return "g++";
			case compiler_kind::clang:
				return "clang++";
			}
		}
	}();

	auto const emit_compile_commands = (
			ctcli::option_value<ctcli::option("build --build-mode")> == build_mode::debug
			&& (ctcli::option_value<ctcli::option("build --emit-compile-commands")> || build_config.emit_compile_commands)
		) && [&]() {
			fs::path const compile_commands_json = "./compile_commands.json";
			return !fs::exists(compile_commands_json)
				|| fs::last_write_time(compile_commands_json) < source_files
					.transform([](auto const &source) { return source.last_modified_time; })
					.max(fs::file_time_type::min());
		}();
	cppb::vector<compile_command> compile_commands;
	if (emit_compile_commands)
	{
		compile_commands.reserve(compilation_units.size());
	}

	auto const job_count = ctcli::option_value<ctcli::option("build --jobs")>;
	bool is_any_cpp = false;
	if (!ctcli::option_value<ctcli::option("build -s")> && job_count > 1)
	{
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


			if (
				auto const object_last_write_time = fs::exists(object_file) ? fs::last_write_time(object_file) : fs::file_time_type::min();
				ctcli::option_value<ctcli::option("build --rebuild")>
				|| object_last_write_time < source.last_modified_time
				|| object_last_write_time < config_last_update
			)
			{
				auto const source_file_name = source_file.generic_string();
				auto const relative_source_file_name = fs::relative(source_file).generic_string();
				auto args = get_compiler_args(build_config, source_file_name, object_file.generic_string());

				if (emit_compile_commands)
				{
					compile_commands.push_back({ source_file_name, args });
				}

				if (compilation_futures.size() == job_count)
				{
					while (!is_any_future_finished())
					{
						using namespace std::chrono_literals;
						compilation_futures.front().second.wait_for(10ms);
					}
					auto const finished = get_finished_future();
					assert(finished != compilation_futures.end());
					compilation_results[finished->first] = finished->second.get();
					compilation_futures.erase(finished);
				}
				fmt::print("({:{}}/{}) {}\n", i + 1, compilation_units_count_width, compilation_units.size(), relative_source_file_name);
				if (ctcli::option_value<ctcli::option("build --verbose")>)
				{
					print_command(is_c_source ? c_compiler : cpp_compiler, std::move(args));
				}
				compilation_futures.push_back({ i, std::async(&run_command, is_c_source ? c_compiler : cpp_compiler, std::move(args), output_kind::null_) });
			}
			else if (emit_compile_commands)
			{
				auto source_file_name = source_file.generic_string();
				auto args = get_compiler_args(build_config, source_file_name, object_file.generic_string());
				compile_commands.push_back({ std::move(source_file_name), std::move(args) });
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
			return 1;
		}
	}
	else
	{
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

			if (
				auto const object_last_write_time = fs::exists(object_file) ? fs::last_write_time(object_file) : fs::file_time_type::min();
				ctcli::option_value<ctcli::option("build --rebuild")>
				|| object_last_write_time < source.last_modified_time
				|| object_last_write_time < config_last_update
			)
			{
				auto const source_file_name = source_file.generic_string();
				auto const relative_source_file_name = fs::relative(source_file).generic_string();
				auto args = get_compiler_args(build_config, source_file_name, object_file.generic_string());

				if (emit_compile_commands)
				{
					compile_commands.push_back({ source_file_name, args });
				}

				fmt::print("({:{}}/{}) {}\n", i + 1, compilation_units_count_width, compilation_units.size(), relative_source_file_name);
				if (ctcli::option_value<ctcli::option("build --verbose")>)
				{
					print_command(is_c_source ? c_compiler : cpp_compiler, std::move(args));
				}
				auto const result = run_command(is_c_source ? c_compiler : cpp_compiler, std::move(args), output_kind::stderr_);
				if (result.exit_code != 0)
				{
					return 1;
				}
			}
			else if (emit_compile_commands)
			{
				auto source_file_name = source_file.generic_string();
				auto args = get_compiler_args(build_config, source_file_name, object_file.generic_string());
				compile_commands.push_back({ std::move(source_file_name), std::move(args) });
			}
		}
	}

	if (compile_commands.size() != 0)
	{
		compile_commands.sort([](auto const &lhs, auto const &rhs) { return lhs.source_file < rhs.source_file; });
		write_compile_commands_json(compile_commands);
	}

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
	auto const last_object_write_time = object_files
		.transform([](auto const &object_file) { return fs::last_write_time(object_file); })
		.max(fs::file_time_type::min());
	if (
		ctcli::option_value<ctcli::option("build --link")>
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

		if (ctcli::option_value<ctcli::option("build -v")>)
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

static int run_project(project_config const &project_config)
{
	std::string error;

	auto const &build_config = [&]() -> auto & {
#ifdef _WIN32
		if (ctcli::option_value<ctcli::option("build --build-mode")> == build_mode::debug)
		{
			return project_config.windows_debug;
		}
		else
		{
			return project_config.windows_release;
		}
#else
		if (ctcli::option_value<ctcli::option("build --build-mode")> == build_mode::debug)
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
		if (ctcli::option_value<ctcli::option("build --build-mode")> == build_mode::debug)
		{
			return fs::path(ctcli::option_value<ctcli::option("build --bin-dir")>) / "windows-debug";
		}
		else
		{
			return fs::path(ctcli::option_value<ctcli::option("build --bin-dir")>) / "windows-release";
		}
#else
		if (ctcli::option_value<ctcli::option("build --build-mode")> == build_mode::debug)
		{
			return fs::path(ctcli::option_value<ctcli::option("build --bin-dir")>) / "linux-debug";
		}
		else
		{
			return fs::path(ctcli::option_value<ctcli::option("build --bin-dir")>) / "linux-release";
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

static int create_new_project(void)
{
	auto const project_directory = fs::path(ctcli::command_value<ctcli::command("new")>);
	auto const src_directory   = project_directory / ctcli::option_value<ctcli::option("new --src-dir")>;
	auto const config_file     = project_directory / ctcli::option_value<ctcli::option("new --config-file")>;

	fs::create_directories(src_directory);
	fs::create_directories(config_file.parent_path());

	output_default_config_json(config_file, ctcli::option_value<ctcli::option("new --src-dir")>);

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

	if (ctcli::is_command_set<ctcli::command("build")>())
	{
		std::string error;

		auto const config_file_path = fs::path(ctcli::option_value<ctcli::option("build --config-file")>);
		auto const project_configs = read_config_json(config_file_path, error);
		if (!error.empty())
		{
			report_error(config_file_path.generic_string(), error);
			return 1;
		}

		auto const &project_config = [&]() -> auto & {
			std::string_view const config_to_build = ctcli::option_value<ctcli::option("build --build-config")>;
			auto const it = std::find_if(
				project_configs.begin(), project_configs.end(),
				[config_to_build](auto const &config) {
					return config_to_build == config.project_name;
				}
			);
			if (it == project_configs.end())
			{
				report_error(
					fmt::format("<command-line>:{}", ctcli::option_index<ctcli::option("build --build-config")>),
					fmt::format("unknown configuration '{}'", config_to_build)
				);
				exit(1);
			}
			return *it;
		}();
		return build_project(project_config, fs::last_write_time(config_file_path));
	}
	else if (ctcli::is_command_set<ctcli::command("run")>())
	{
		std::string error;

		auto const config_file_path = fs::path(ctcli::option_value<ctcli::option("build --config-file")>);
		auto const project_configs = read_config_json(config_file_path, error);
		if (!error.empty())
		{
			report_error(config_file_path.generic_string(), error);
			return 1;
		}

		auto const &project_config = [&]() -> auto & {
			std::string_view const config_to_build = ctcli::option_value<ctcli::option("build --build-config")>;
			auto const it = std::find_if(
				project_configs.begin(), project_configs.end(),
				[config_to_build](auto const &config) {
					return config_to_build == config.project_name;
				}
			);
			if (it == project_configs.end())
			{
				report_error(
					fmt::format("<command-line>:{}", ctcli::option_index<ctcli::option("build --build-config")>),
					fmt::format("unknown configuration '{}'", config_to_build)
				);
				exit(1);
			}
			return *it;
		}();
		auto const build_result = build_project(project_config, fs::last_write_time(config_file_path));
		if (build_result != 0)
		{
			return build_result;
		}
		return run_project(project_config);
	}
	else if (ctcli::is_command_set<ctcli::command("new")>())
	{
		return create_new_project();
	}

	return 0;
}
