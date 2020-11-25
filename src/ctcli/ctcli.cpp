#include "ctcli.h"

namespace ctcli
{

bool alphabetical_compare(string_view lhs, string_view rhs)
{
	auto lhs_it = lhs.begin();
	auto rhs_it = rhs.begin();
	auto const lhs_end = lhs.end();
	auto const rhs_end = rhs.end();

	auto const is_upper = [](char c) {
		return c >= 'A' && c <= 'Z';
	};
	auto const to_upper = [](char c) -> char {
		if (c >= 'a' && c <= 'z')
		{
			static_assert('a' > 'A');
			return c - ('a' - 'A');
		}
		else
		{
			return c;
		}
	};

	for (; lhs_it != lhs_end && rhs_it != rhs_end; ++lhs_it, ++rhs_it)
	{
		auto const lhs_c = to_upper(*lhs_it);
		auto const rhs_c = to_upper(*rhs_it);
		auto const lhs_is_upper = is_upper(*lhs_it);
		auto const rhs_is_upper = is_upper(*rhs_it);

		if (lhs_c < rhs_c || (lhs_c == rhs_c && !lhs_is_upper && rhs_is_upper))
		{
			return true;
		}
		else if (lhs_c > rhs_c || (lhs_c == rhs_c && lhs_is_upper && !rhs_is_upper))
		{
			return false;
		}
		assert(lhs_c == rhs_c && lhs_is_upper == rhs_is_upper);
	}

	return lhs_it == lhs_end && rhs_it != rhs_end;
}

static vector<string_view> split_words(string_view str)
{
	vector<string_view> result = {};
	auto it = str.begin();
	auto const end = str.end();
	while (it != end)
	{
		auto const next_space = std::find(it, str.end(), ' ');
		result.emplace_back(it, next_space);
		if (next_space == end)
		{
			break;
		}

		it = next_space + 1;
	}

	return result;
}

static string format_long_help_string(
	string_view help_str,
	std::size_t initial_indent_width,
	std::size_t usage_width,
	std::size_t column_limit
)
{
	auto const next_line_indent_width = initial_indent_width + usage_width;
	auto const help_str_width = column_limit - next_line_indent_width;
	auto const indentation = fmt::format("{:{}}", "", next_line_indent_width);
	assert(help_str.length() > help_str_width);
	auto const words = split_words(help_str);

	string result = "";
	std::size_t column = 0;
	bool first = true;
	for (auto const word : words)
	{
		assert(column <= help_str_width);
		auto const len = word.length();
		// -1 because of the space in the front
		if (column != 0 && len + column > help_str_width - 1)
		{
			result += '\n';
			result += indentation;
			column = 0;
		}
		else if (!first)
		{
			result += ' ';
			column += 1;
		}

		if (len > help_str_width)
		{
			auto const lines_count = len / help_str_width + 1;
			auto const last_column = len % help_str_width;
			column = last_column;
			for (std::size_t i = 0; i < lines_count; ++i)
			{
				if (i != 0)
				{
					result += '\n';
					result += indentation;
				}
				result += word.substr(i * help_str_width, help_str_width);
			}
		}
		else
		{
			result += word;
			column += len;
		}

		first = false;
	}

	return result;
}

string get_help_string(
	vector<string> const &usages,
	vector<string> const &helps,
	std::size_t initial_indent_width,
	std::size_t usage_width,
	std::size_t column_limit
)
{
	auto const initial_indent = fmt::format("{:{}}", "", initial_indent_width);

	string result = "";

	assert(usages.size() == helps.size());
	for (std::size_t i = 0; i < usages.size(); ++i)
	{
		auto const &usage = usages[i];
		auto const &help  = helps[i];
		auto const formatted_help = help.length() > (column_limit - usage_width - initial_indent_width)
			? format_long_help_string(help, initial_indent_width, usage_width, column_limit)
			: help;

		if (usage.length() >= usage_width)
		{
			result += initial_indent;
			result += usage;
			result += fmt::format("\n{:{}}{}\n", "", initial_indent_width + usage_width, formatted_help);
		}
		else
		{
			result += fmt::format("{}{:{}}{}\n", initial_indent, usage, usage_width, formatted_help);
		}
	}

	return result;
}

} // namespace ctcli
