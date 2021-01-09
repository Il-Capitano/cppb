#include "analyze.h"
#include <bits/fs_path.h>
#include <compare>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <cassert>
#include <string_view>
#include <functional>
#include <rapidjson/document.h>
#include <rapidjson/error/error.h>
#include <rapidjson/writer.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/ostreamwrapper.h>
#include <rapidjson/istreamwrapper.h>

struct include_file
{
	std::string path;
	bool is_library;
};

static std::string get_file(fs::path const &file_path)
{
	std::ifstream file(file_path);
	file.seekg(std::ios_base::end);
	auto const size = file.tellg();
	assert(size >= 0);
	file.seekg(std::ios_base::beg);

	std::string result;
	result.reserve(static_cast<std::size_t>(size));
	result.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
	return result;
}

static cppb::vector<include_file> get_includes(fs::path const &file_path)
{
	auto const file = get_file(file_path);
	auto it = file.begin();
	auto const end = file.end();

	cppb::vector<include_file> result;

	auto const find_next_hash = [&]() {
		bool is_line_begin = true;
		while (it != end)
		{
			switch (*it)
			{
			case '\n':
				is_line_begin = true;
				++it;
				break;
			case ' ':
			case '\t':
			case '\r':
				++it;
				break;
			case '#':
				if (is_line_begin)
				{
					return;
				}
				++it;
				break;
			case '/':
				if (it + 1 != end && *(it + 1) == '*')
				{
					++it; ++it; // '/*'
					while (it != end && it + 1 != end && !(*it == '*' && *(it + 1) == '/'))
					{
						++it;
					}

					if (it + 1 == end)
					{
						++it;
					}
					else if (it != end)
					{
						++it; ++it; // '*/'
					}
				}
				else if (it + 1 != end && *(it + 1) == '/')
				{
					++it; ++it; // '//'
					while (it != end && *it != '\n')
					{
						++it;
					}
				}
				else
				{
					is_line_begin = false;
					++it;
				}
				break;
			default:
				is_line_begin = false;
				++it;
				break;
			}
		}
	};

	auto const is_include_directive = [&]() {
		if (it == end)
		{
			return false;
		}
		assert(*it == '#');
		++it;
		// skip whitespace
		while (it != end && (*it == ' ' || *it == '\t'))
		{
			++it;
		}

		auto const directive_begin = it;
		while (it != end && [&]() {
			auto const c = *it;
			return (c >= '0' && c <= '9')
				|| (c >= 'a' && c <= 'z')
				|| (c >= 'A' && c <= 'Z')
				|| c == '_';
		}())
		{
			++it;
		}
		auto const directive_end = it;
		auto const directive_name = std::string_view(&*directive_begin, static_cast<std::size_t>(directive_end - directive_begin));
		return directive_name == "include";
	};

	auto const get_include_file = [&]() {
		// skip whitespace
		while (it != end && (*it == ' ' || *it == '\t'))
		{
			++it;
		}

		if (it == end)
		{
			return;
		}

		auto const open_char = *it;
		if (open_char == '<')
		{
			++it;
			auto const file_name_begin = it;
			auto const file_name_end   = std::find(it, end, '>');
			it = file_name_end;
			result.push_back({ std::string(file_name_begin, file_name_end), true });
		}
		else if (open_char == '"')
		{
			++it;
			auto const file_name_begin = it;
			auto const file_name_end   = std::find(it, end, '"');
			it = file_name_end;
			result.push_back({ std::string(file_name_begin, file_name_end), false });
		}
	};

	while (it != end)
	{
		find_next_hash();
		if (is_include_directive())
		{
			get_include_file();
		}
	}

	return result;
}

static cppb::vector<fs::path> get_dependencies(
	fs::path const &source,
	cppb::vector<fs::path> const &include_directories
)
{
	auto const source_directory = source.parent_path();
	return get_includes(source)
		.transform([&source_directory, &include_directories](auto const &include) {
			if (include.is_library)
			{
				for (auto const &dir : include_directories)
				{
					auto const file = dir / include.path;
					if (fs::exists(file))
					{
						return fs::absolute(file);
					}
				}
			}
			else
			{
				auto const relative_path = source_directory / include.path;
				if (fs::exists(relative_path))
				{
					return fs::absolute(relative_path);
				}

				for (auto const &dir : include_directories)
				{
					auto const file = dir / include.path;
					if (fs::exists(file))
					{
						return fs::absolute(file);
					}
				}
			}

			return fs::path{};
		})
		.filter([](auto const &path) { return !path.empty(); })
		.collect<cppb::vector>();
}

static source_file analyze_source_file(
	fs::path const &source,
	cppb::vector<fs::path> const &include_directories
)
{
	return { source, get_dependencies(source, include_directories), fs::file_time_type::min() };
}

cppb::vector<fs::path> get_source_files_in_directory(fs::path const &dir)
{
	return ranges::basic_range{ fs::recursive_directory_iterator(dir), fs::recursive_directory_iterator() }
		.filter   ([](auto const &path) { return path.is_regular_file(); })
		.transform([](auto const &path) -> auto const & { return path.path(); })
		.filter   ([](auto const &path) {
			auto const extension = path.extension().generic_string();
			return source_extensions.is_any([&extension](auto const source_extension) { return extension == source_extension; });
		})
		.transform([](auto const &path) { return fs::absolute(path).lexically_normal(); })
		.collect<cppb::vector>();
}

static void add_source_file(
	cppb::vector<std::size_t> &hashes,
	cppb::vector<source_file> &source_files,
	fs::path const &source,
	cppb::vector<fs::path> const &include_directories,
	fs::file_time_type dependency_file_last_update
)
{
	auto const find_source_file = [&hashes, &source_files](std::size_t hash_, fs::path const &source_) {
		assert(hashes.size() == source_files.size());
		auto hashes_it = hashes.begin();
		auto source_files_it = source_files.begin();
		auto const source_files_end = source_files.end();
		for (; source_files_it != source_files_end; ++source_files_it, ++hashes_it)
		{
			if (*hashes_it == hash_ && source_files_it->file_path == source_)
			{
				return source_files_it;
			}
		}
		return source_files_it;
	};

	auto const hash = fs::hash_value(source);
	auto const source_it = find_source_file(hash, source);

	if (source_it != source_files.end())
	{
		fmt::format("duplicate file '{}'\n", source_it->file_path.generic_string());
		return;
	}

	auto const source_index = source_files.size();
	source_files.emplace_back(analyze_source_file(source, include_directories));
	hashes.emplace_back(hash);

	for (auto const &dep : source_files[source_index].dependencies)
	{
		add_source_file(hashes, source_files, dep, include_directories, dependency_file_last_update);
	}

	auto const dependencies_last_modified = source_files[source_index].dependencies
		.transform([&](auto const &dep) { return find_source_file(fs::hash_value(dep), dep); })
		.filter([&](auto const it) { return it != source_files.end(); })
		.transform([](auto const it) { return it->last_modified_time; })
		.max(fs::file_time_type::min());
	source_files[source_index].last_modified_time = std::max(fs::last_write_time(source), dependencies_last_modified);
}

void analyze_source_files(
	cppb::vector<fs::path> const &files,
	cppb::vector<fs::path> const &include_directories,
	cppb::vector<source_file> &sources,
	fs::file_time_type dependency_file_last_update,
	fs::file_time_type config_last_update
)
{
	auto non_updated_sources = sources
		.filter([&](auto const &source) {
			return fs::exists(source.file_path)
				&& source.last_modified_time < dependency_file_last_update
				&& config_last_update < dependency_file_last_update;
		})
		.collect<cppb::vector>();
	cppb::vector<std::size_t> hashes = non_updated_sources
		.transform([](auto const &source) { return fs::hash_value(source.file_path); })
		.collect<cppb::vector>();

	for (auto const &file : files)
	{
		add_source_file(hashes, non_updated_sources, file, include_directories, dependency_file_last_update);
	}

	sources = std::move(non_updated_sources);
}

static fs::file_time_type get_and_fill_last_modified_time(fs::path const &file, cppb::vector<std::size_t> const &hashes, cppb::vector<source_file> &sources)
{
	auto const find_source_file = [&hashes, &sources](std::size_t hash_, fs::path const &source_) {
		assert(hashes.size() == sources.size());
		auto hashes_it = hashes.begin();
		auto sources_it = sources.begin();
		auto const source_files_end = sources.end();
		for (; sources_it != source_files_end; ++sources_it, ++hashes_it)
		{
			if (*hashes_it == hash_ && sources_it->file_path == source_)
			{
				return sources_it;
			}
		}
		return sources_it;
	};

	auto const hash = fs::hash_value(file);
	auto const it = find_source_file(hash, file);
	assert(it != sources.end());
	if (it->last_modified_time != fs::file_time_type::min())
	{
		return it->last_modified_time;
	}
	// set it->last_modified_time first to avoid infinite recursion with circular dependencies
	it->last_modified_time = fs::last_write_time(file);
	it->last_modified_time = it->dependencies
		.transform([&](auto const &dependency) { return get_and_fill_last_modified_time(dependency, hashes, sources); })
		.max(it->last_modified_time);
	return it->last_modified_time;
}

void fill_last_modified_times(cppb::vector<source_file> &sources)
{
	auto const hashes = sources
		.transform([](auto const &source) { return fs::hash_value(source.file_path); })
		.collect<cppb::vector>();
	for (auto &source : sources)
	{
		source.last_modified_time = source.dependencies
			.transform([&](auto const &dependency) { return get_and_fill_last_modified_time(dependency, hashes, sources); })
			.max(fs::last_write_time(source.file_path));
	}
}

void write_dependency_json(fs::path const &output_path, cppb::vector<source_file> const &sources)
{
	rapidjson::Document d;
	d.SetObject();
	auto object = d.GetObject();
	sources.for_each([&](auto const &source) {
		rapidjson::Value value;
		value.SetArray();
		assert(value.IsArray());
		auto array = value.GetArray();
		source.dependencies.for_each([&](auto const &dep) {
			array.PushBack(
				rapidjson::Value(dep.generic_string().c_str(), d.GetAllocator()),
				d.GetAllocator()
			);
		});
		object.AddMember(
			rapidjson::Value(source.file_path.generic_string().c_str(), d.GetAllocator()),
			std::move(value),
			d.GetAllocator()
		);
	});

	fs::create_directories(output_path.parent_path());
	std::ofstream output(output_path);
	if (!output.is_open())
	{
		return;
	}

	rapidjson::OStreamWrapper output_wrapper(output);

	rapidjson::PrettyWriter writer(output_wrapper);
	d.Accept(writer);
}

cppb::vector<source_file> read_dependency_json(fs::path const &dep_file_path, std::string &error)
{
	std::ifstream input(dep_file_path);
	if (!input.is_open())
	{
		return {};
	}

	rapidjson::IStreamWrapper input_wrapper(input);
	rapidjson::Document d;
	d.ParseStream(input_wrapper);

	if (d.HasParseError())
	{
		error = fmt::format("an error occurred while parsing '{}'", dep_file_path.generic_string());
		return {};
	}

	if (!d.IsObject())
	{
		error = "top level value in dependency file must be an 'Object'";
		return {};
	}

	cppb::vector<source_file> result;

	auto object = d.GetObject();
	// bullshit iteration because of C++20 breaking things with operator <=>
	auto const begin = object.begin();
	auto const end   = object.end();
	for (auto it = begin; it.operator->() != end.operator->(); ++it)
	{
		auto const &member = *it;
		assert(member.name.IsString());
		std::string_view const name = member.name.GetString();
		auto name_path = fs::path(name).lexically_normal();

		if (!fs::exists(name_path))
		{
			continue;
		}
		if (!member.value.IsArray())
		{
			error = fmt::format("value of member '{}' in dependency file must be an 'Array'", name);
			return {};
		}
		auto value_array = member.value.GetArray();
		cppb::vector<fs::path> dependencies;
		dependencies.reserve(value_array.Size());
		for (auto const &value : value_array)
		{
			if (!value.IsString())
			{
				error = fmt::format("array element in value of member '{}' in dependency file must be a 'String'", name);
				return {};
			}
			auto path = fs::path(value.GetString()).lexically_normal();
			if (fs::exists(path))
			{
				dependencies.emplace_back(std::move(path));
			}
		}
		result.push_back({ std::move(name_path), std::move(dependencies), fs::file_time_type::min() });
	}

	return result;
}

void write_compile_commands_json(cppb::vector<compile_command> const &compile_commands)
{
	rapidjson::Document d;
	d.SetArray();
	assert(d.IsArray());
	auto array = d.GetArray();
	auto const directory = fs::current_path().generic_string();

	for (auto const &command : compile_commands)
	{
		rapidjson::Value value;
		value.SetObject();
		auto object = value.GetObject();

		object.AddMember(
			"directory",
			rapidjson::Value(directory.c_str(), d.GetAllocator()),
			d.GetAllocator()
		);
		object.AddMember(
			"file",
			rapidjson::Value(command.source_file.c_str(), d.GetAllocator()),
			d.GetAllocator()
		);
		rapidjson::Value args;
		args.SetArray();
		auto args_array = args.GetArray();
		for (auto const &arg : command.args)
		{
			args_array.PushBack(rapidjson::Value(arg.c_str(), d.GetAllocator()), d.GetAllocator());
		}
		object.AddMember(
			"arguments",
			std::move(args),
			d.GetAllocator()
		);

		array.PushBack(std::move(value), d.GetAllocator());
	}

	std::ofstream output("./compile_commands.json");
	if (!output.is_open())
	{
		fmt::print("error while writing compile_commands.json");
		return;
	}

	rapidjson::OStreamWrapper output_wrapper(output);

	rapidjson::PrettyWriter writer(output_wrapper);
	d.Accept(writer);
}
