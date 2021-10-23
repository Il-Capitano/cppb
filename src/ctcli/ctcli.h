#ifndef CTCLI_CTCLI_H
#define CTCLI_CTCLI_H

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <array>
#include <initializer_list>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <fmt/format.h>

#define CTCLI_UNREACHABLE ((void)fmt::print(stderr, "hit unreachable code at {}:{}\n", __FILE__, __LINE__), __builtin_trap())

#if defined(__cpp_exceptions)
#define CTCLI_SYNTAX_ASSERT(cond, message) do {           \
    if (!(cond)) { throw invalid_syntax_error(message); } \
} while (false)
#define CTCLI_THROW(x) throw x
#else
#define CTCLI_SYNTAX_ASSERT(cond, message) do {           \
    if (!(cond)) { fmt::print(stderr, message); std::abort(); } \
} while (false)
#define CTCLI_THROW(x)
#endif // exceptions

namespace ctcli
{

#if __cplusplus < 202002L || defined(__clang__)

struct string_view : std::string_view
{
	using std::string_view::string_view;

	constexpr string_view(char const *str)
		: std::string_view(str)
	{}

	constexpr string_view(std::string_view str)
		: std::string_view(str)
	{}

	constexpr string_view(char const *begin, char const *end)
		: std::string_view(begin, static_cast<std::size_t>(end - begin))
	{}

	constexpr bool starts_with(char c) const noexcept
	{
		return !this->empty() && this->front() == c;
	}

	constexpr bool starts_with(string_view str) const noexcept
	{
		if (this->size() < str.size())
		{
			return false;
		}
		auto it = this->begin();
		auto str_it = str.begin();
		auto str_end = str.end();
		for (; str_it != str_end; ++str_it, ++it)
		{
			if (*it != *str_it)
			{
				return false;
			}
		}
		return true;
	}

	constexpr bool ends_with(string_view str) const noexcept
	{
		if (this->size() < str.size())
		{
			return false;
		}
		auto it = this->begin() + (this->size() - str.size());
		auto str_it = str.begin();
		auto str_end = str.end();
		for (; str_it != str_end; ++str_it, ++it)
		{
			if (*it != *str_it)
			{
				return false;
			}
		}
		return true;
	}

	constexpr bool ends_with(char c) const noexcept
	{
		return !this->empty() && this->back() == c;
	}

	constexpr string_view substr(size_type pos = 0, size_type count = npos) const noexcept
	{
		return string_view(this->std::string_view::substr(pos, count));
	}
};

struct string : std::string
{
	using std::string::string;

	string(std::string const &str)
		: std::string(str)
	{}

	string(std::string &&str)
		: std::string(std::move(str))
	{}

	bool starts_with(char c) const noexcept
	{ return string_view(*this).starts_with(c); }

	bool starts_with(string_view str) const noexcept
	{ return string_view(*this).starts_with(str); }

	bool ends_with(string_view str) const noexcept
	{ return string_view(*this).ends_with(str); }

	bool ends_with(char c) const noexcept
	{ return string_view(*this).ends_with(c); }

	operator string_view(void) const noexcept
	{ return string_view(std::string_view(*this)); }
};

#else

using string      = std::string;
using string_view = std::string_view;

#endif // c++20

using std_string = std::string;
using std_string_view = std::string_view;

template<typename T, std::size_t N>
using array = std::array<T, N>;

template<typename T>
using optional = std::optional<T>;

template<typename T>
using vector = std::vector<T>;


class invalid_syntax_error final : public std::runtime_error
{
	using std::runtime_error::runtime_error;
};

namespace internal
{

// The functions defined here should only be used at compile time, for building the
// `command_line_options` array.

using string_iter = string_view::const_iterator;
using char_type   = string_view::value_type;

/// Returns whether `c` is a valid character that can appear in a flag name
/// usually it's an alphanumeric or dash, but we allow other characters
/// that are not used for anything else.
constexpr bool is_valid_flag_char(char_type c) noexcept
{
	return c > ' '
		&& c != '\u007f' // DEL
		&& c != ','
		&& c != '='
		&& c != '|'
		&& c != '\'' && c != '"'
		&& c != '<' && c != '>'
		&& c != '[' && c != ']'
		&& c != '(' && c != ')'
		&& c != '{' && c != '}';
}

/// Advances `it` until it's not pointing to a valid flag character or it's
/// equal to end.  If `it` doesn't point to a valid flag character to begin with,
/// this function does nothing and just returns
constexpr void consume_flag_name(string_iter &it, string_iter end) noexcept
{
	while (it != end && is_valid_flag_char(*it))
	{
		++it;
	}
}

/// Advances `it` to the end of a value with the syntax '<value>' or '{val1|val2|val3}'.
/// Throws ctcli::invalid_syntax_error if the syntax is invalid.
constexpr void consume_value(string_view usage, string_iter &it, string_iter end)
{
	CTCLI_SYNTAX_ASSERT(it != end && (*it == '<' || *it == '{'), fmt::format("usage '{}' has invalid syntax, values must start with '<' or '{{'", usage));
	if (*it == '<')
	{
		++it; // '<'
		consume_flag_name(it, end);
		CTCLI_SYNTAX_ASSERT(
			it != end && *it == '>',
			fmt::format("usage '{}' has invalid syntax, expected '>' as end of value after '{}'", usage, string_view(usage.begin(), static_cast<std::size_t>(it - usage.begin())))
		);
		++it; // '>'
	}
	else // if (*it == '{')
	{
		++it; // '{'
		do
		{
			auto const choice_begin = it;
			consume_flag_name(it, end);
			auto const choice_end = it;
			auto const choice_name = string_view(choice_begin, choice_end);
			CTCLI_SYNTAX_ASSERT(
				choice_name != "",
				fmt::format("usage '{}' has invalid syntax, expected choice name after '{}'", usage, string_view(usage.begin(), static_cast<std::size_t>(it - usage.begin())))
			);
		} while (it != end && *it == '|' && ((void)++it, true));
		CTCLI_SYNTAX_ASSERT(
			it != end && *it == '}',
			fmt::format("usage '{}' has invalid syntax, expected '}}' as end of value after '{}'", usage, string_view(usage.begin(), static_cast<std::size_t>(it - usage.begin())))
		);
		++it; // '}'
	}
}

/// Checks whether `usage` has valid syntax.
/// Throws ctcli::invalid_syntax_error if the syntax is invalid
constexpr void check_group_elment_syntax(string_view usage)
{
	auto it = usage.begin();
	auto const end = usage.end();
	CTCLI_SYNTAX_ASSERT(
		it != end && is_valid_flag_char(*it),
		fmt::format("usage '{}' has invalid syntax, usages must start with a flag name", usage)
	);
	consume_flag_name(it, end);
	if (it == end)
	{
		return;
	}
	if (*it == '=')
	{
		++it; // '='
		consume_value(usage, it, end);
		CTCLI_SYNTAX_ASSERT(
			it == end,
			fmt::format("usage '{}' has invalid syntax, expected end of usage after '{}'", usage, string_view(usage.begin(), static_cast<std::size_t>(it - usage.begin())))
		);
		return;
	}
	else
	{
		CTCLI_SYNTAX_ASSERT(
			it == end,
			fmt::format("usage '{}' has invalid syntax, expected '=' or end of usage after '{}'", usage, string_view(usage.begin(), static_cast<std::size_t>(it - usage.begin())))
		);
		return;
	}
}

/// Checks whether `usage` has valid syntax.
/// Throws ctcli::invalid_syntax_error if the syntax is invalid
constexpr void check_flag_syntax(string_view usage)
{
	auto it = usage.begin();
	auto const end = usage.end();
	CTCLI_SYNTAX_ASSERT(
		it != end && *it == '-',
		fmt::format("usage '{}' has invalid syntax, usages must start with '-'", usage)
	);
	consume_flag_name(it, end);
	if (it == end)
	{
		return;
	}
	switch (*it)
	{
	case ' ':
		++it; // ' '
		consume_value(usage, it, end);
		CTCLI_SYNTAX_ASSERT(
			it == end,
			fmt::format("usage '{}' has invalid syntax, expected end of usage after '{}'", usage, string_view(usage.begin(), static_cast<std::size_t>(it - usage.begin())))
		);
		return;
	case '=':
		++it; // '='
		consume_value(usage, it, end);
		CTCLI_SYNTAX_ASSERT(
			it == end,
			fmt::format("usage '{}' has invalid syntax, expected end of usage after '{}'", usage, string_view(usage.begin(), static_cast<std::size_t>(it - usage.begin())))
		);
		return;
	case ',':
		++it; // ','
		CTCLI_SYNTAX_ASSERT(
			it != end && *it == ' ',
			fmt::format("usage '{}' has invalid syntax, expected ' ' after '{}'", usage, string_view(usage.begin(), static_cast<std::size_t>(it - usage.begin())))
		);
		++it; // ' '
		CTCLI_SYNTAX_ASSERT(
			it != end && *it == '-',
			fmt::format("usage '{}' has invalid syntax, expected a flag name after '{}'", usage, string_view(usage.begin(), static_cast<std::size_t>(it - usage.begin())))
		);
		consume_flag_name(it, end);
		if (it == end)
		{
			return;
		}
		CTCLI_SYNTAX_ASSERT(
			*it == ' ',
			fmt::format("usage '{}' has invalid syntax, expected end of usage or ' ' after '{}'", usage, string_view(usage.begin(), static_cast<std::size_t>(it - usage.begin())))
		);
		++it; // ' '
		consume_value(usage, it, end);
		CTCLI_SYNTAX_ASSERT(
			it == end,
			fmt::format("usage '{}' has invalid syntax, expected end of usage after '{}'", usage, string_view(usage.begin(), static_cast<std::size_t>(it - usage.begin())))
		);
		return;
	default:
		CTCLI_SYNTAX_ASSERT(
			it == end,
			fmt::format("usage '{}' has invalid syntax, expected ' ' or '=' or ',' or end of usage after '{}'", usage, string_view(usage.begin(), static_cast<std::size_t>(it - usage.begin())))
		);
		return;
	}
}

/// Checks whether `usage` has valid syntax for commands.
/// Throws ctcli::invalid_syntax_error if the syntax is invalid
constexpr void check_command_syntax(string_view usage)
{
	auto it = usage.begin();
	auto const end = usage.end();
	CTCLI_SYNTAX_ASSERT(
		it != end && is_valid_flag_char(*it),
		fmt::format("usage '{}' has invalid syntax, usage must have a name", usage)
	);
	consume_flag_name(it, end);
	if (it == end)
	{
		return;
	}
	switch (*it)
	{
	case ' ':
		++it; // ' '
		consume_value(usage, it, end);
		CTCLI_SYNTAX_ASSERT(
			it == end,
			fmt::format("usage '{}' has invalid syntax, expected end of usage after '{}'", usage, string_view(usage.begin(), static_cast<std::size_t>(it - usage.begin())))
		);
		return;
	case ',':
		++it; // ','
		CTCLI_SYNTAX_ASSERT(
			it != end && *it == ' ',
			fmt::format("usage '{}' has invalid syntax, expected ' ' after '{}'", usage, string_view(usage.begin(), static_cast<std::size_t>(it - usage.begin())))
		);
		++it; // ' '
		CTCLI_SYNTAX_ASSERT(
			it != end && *it == '-',
			fmt::format("usage '{}' has invalid syntax, expected a flag name after '{}'", usage, string_view(usage.begin(), static_cast<std::size_t>(it - usage.begin())))
		);
		consume_flag_name(it, end);
		if (it == end)
		{
			return;
		}
		CTCLI_SYNTAX_ASSERT(
			*it == ' ',
			fmt::format("usage '{}' has invalid syntax, expected end of usage or ' ' after '{}'", usage, string_view(usage.begin(), static_cast<std::size_t>(it - usage.begin())))
		);
		++it; // ' '
		consume_value(usage, it, end);
		CTCLI_SYNTAX_ASSERT(
			it == end,
			fmt::format("usage '{}' has invalid syntax, expected end of usage after '{}'", usage, string_view(usage.begin(), static_cast<std::size_t>(it - usage.begin())))
		);
		return;
	default:
		CTCLI_SYNTAX_ASSERT(
			it == end,
			fmt::format("usage '{}' has invalid syntax, expected ' ' or ',' or end of usage after '{}'", usage, string_view(usage.begin(), static_cast<std::size_t>(it - usage.begin())))
		);
		return;
	}
}

/// Returns the flag names in `usage` without the dashes.  The first element is the single char
/// variant (if any), the second element is the long variant.
constexpr std::pair<string_view, string_view> get_flag_names(string_view usage) noexcept
{
	auto it = usage.begin();
	auto const end = usage.end();

	auto const first_flag_name_begin = it;
	while (it != end && is_valid_flag_char(*it))
	{
		++it;
	}
	auto const first_flag_name = string_view(first_flag_name_begin, static_cast<std::size_t>(it - first_flag_name_begin));

	if (it == end || *it != ',')
	{
		if (first_flag_name.starts_with('-') && first_flag_name.length() == 2)
		{
			return { first_flag_name.substr(1), string_view{} };
		}
		else if (first_flag_name.starts_with("--"))
		{
			return { string_view{}, first_flag_name.substr(2) };
		}
		else
		{
			return { string_view{}, first_flag_name };
		}
	}

	++it; // ','
	assert(it != end && *it == ' ');
	++it; // ' '
	assert(it != end && *it == '-');

	auto const second_flag_name_begin = it;
	while (it != end && is_valid_flag_char(*it))
	{
		++it;
	}
	auto const second_flag_name = string_view(second_flag_name_begin, static_cast<std::size_t>(it - second_flag_name_begin));
	if (first_flag_name.starts_with('-') && first_flag_name.length() == 2)
	{
		assert(second_flag_name.starts_with("--") && second_flag_name.length() > 2);
		return { first_flag_name.substr(1), second_flag_name.substr(2) };
	}
	else
	{
		return { first_flag_name, second_flag_name };
	}
}

/// Returns the flag names in `usage` with dashes included.  The first element is the single char
/// variant (if any), the second element is the long variant.
constexpr std::pair<string_view, string_view> get_flag_names_with_dashes(string_view usage) noexcept
{
	auto it = usage.begin();
	auto const end = usage.end();

	auto const first_flag_name_begin = it;
	while (it != end && is_valid_flag_char(*it))
	{
		++it;
	}
	auto const first_flag_name = string_view(first_flag_name_begin, static_cast<std::size_t>(it - first_flag_name_begin));

	if (it == end || *it != ',')
	{
		if (first_flag_name.starts_with('-') && first_flag_name.length() == 2)
		{
			return { first_flag_name, string_view{} };
		}
		else
		{
			assert(first_flag_name.starts_with("--"));
			return { string_view{}, first_flag_name };
		}
	}

	++it; // ','
	assert(it != end && *it == ' ');
	++it; // ' '
	assert(it != end && *it == '-');

	auto const second_flag_name_begin = it;
	while (it != end && is_valid_flag_char(*it))
	{
		++it;
	}
	auto const second_flag_name = string_view(second_flag_name_begin, static_cast<std::size_t>(it - second_flag_name_begin));
	assert(first_flag_name.starts_with('-') && first_flag_name.length() == 2);
	assert(second_flag_name.starts_with("--") && second_flag_name.length() > 2);

	return { first_flag_name, second_flag_name };
}

/// Same as `get_flag_names`, but for flags like '--flag-name=<value>' the '=' is included in the
/// returned returned string.
constexpr std::pair<string_view, string_view> get_flag_names_with_equals(string_view usage) noexcept
{
	auto it = usage.begin();
	auto const end = usage.end();

	auto const first_flag_name_begin = it;
	while (it != end && is_valid_flag_char(*it))
	{
		++it;
	}

	if (it == end || (*it != ',' && *it != '='))
	{
		return { string_view(first_flag_name_begin, it), "" };
	}

	if (*it == '=')
	{
		return { string_view(first_flag_name_begin, it + 1), "" };
	}

	auto const first_flag_name = string_view(first_flag_name_begin, static_cast<std::size_t>(it - first_flag_name_begin));

	++it; // ','
	assert(it != end && *it == ' ');
	++it; // ' '
	assert(it != end && *it == '-');

	auto const second_flag_name_begin = it;
	while (it != end && is_valid_flag_char(*it))
	{
		++it;
	}
	auto const second_flag_name = string_view(second_flag_name_begin, static_cast<std::size_t>(it - second_flag_name_begin));

	return { first_flag_name, second_flag_name };
}

/// Returns whether the `c` is contained in `str`
constexpr bool string_contains(string_view str, char_type c)
{
#if __cplusplus < 202002L
	for (auto const str_c : str)
	{
		if (c == str_c)
		{
			return true;
		}
	}
	return false;
#else
	return std::find(str.begin(), str.end(), c) != str.end();
#endif // c++20
}

/// Returns whether `usage` is a simple bool flag (e.g. '-h, --help')
constexpr bool is_bool_flag(string_view usage) noexcept
{
	return !string_contains(usage, '<')
		&& !string_contains(usage, '{');
}

/// Returns whether `usage` is an argument flag (e.g. '-o <output>')
constexpr bool is_argument_flag(string_view usage) noexcept
{
	return !string_contains(usage, '=')
		&& (
			string_contains(usage, '<')
			|| string_contains(usage, '{')
		);
}

/// Returns whether `usage` is an equals flag (e.g. '--emit=<type>')
constexpr bool is_equals_flag(string_view usage) noexcept
{
	return string_contains(usage, '=');
}

/// Returns whether the value in `usage` is a choice value
constexpr bool is_choice_value(string_view usage) noexcept
{
	return string_contains(usage, '{');
}

/// Returns the number of choices in a choice value, or 0 if it's not a choice value
constexpr std::size_t get_value_choice_count(string_view usage) noexcept
{
	return is_choice_value(usage)
		? static_cast<std::size_t>(std::count(usage.begin(), usage.end(), '|') + 1)
		: 0;
}

/// Parses the choice value in `usage` and collects the possible choices.
/// Should be used with `get_value_choice_count` to get `N`.
template<std::size_t N>
constexpr array<string_view, N> get_value_choices(string_view usage) noexcept
{
	static_assert(N != 0);
	array<string_view, N> result{};

	check_flag_syntax(usage);
	auto const begin = std::find(usage.begin(), usage.end(), '{') + 1;
	auto const end   = std::find(usage.begin(), usage.end(), '}');
	std::size_t i = 0;
	for (auto it = begin; it != end; ++i)
	{
		auto const choice_begin = it;
		consume_flag_name(it, end);
		auto const choice_end = it;
		if (it != end)
		{
			assert(*it == '|');
			++it;
		}
		result[i] = string_view(choice_begin, choice_end);
	}
	assert(i == N);
	return result;
}

} // namespace internal


/// An enum to represent the type of the argument for an option.
enum class arg_type
{
	none,
	integer, unsigned_integer,
	floating_point,
	boolean,
	int8, int16, int32, int64,
	uint8, uint16, uint32, uint64,
	float32, float64,
	string,
};

namespace internal
{

constexpr string_view arg_type_to_string(arg_type type) noexcept
{
	switch (type)
	{
	case arg_type::none:
		return "none";
	case arg_type::integer:
		return "integer";
	case arg_type::unsigned_integer:
		return "unsigned integer";
	case arg_type::floating_point:
		return "floating point";
	case arg_type::boolean:
		return "boolean";
	case arg_type::int8:
		return "int8";
	case arg_type::int16:
		return "int16";
	case arg_type::int32:
		return "int32";
	case arg_type::int64:
		return "int64";
	case arg_type::uint8:
		return "uint8";
	case arg_type::uint16:
		return "uint16";
	case arg_type::uint32:
		return "uint32";
	case arg_type::uint64:
		return "uint64";
	case arg_type::float32:
		return "float32";
	case arg_type::float64:
		return "float64";
	case arg_type::string:
		return "string";
	}
	return "";
}

template<arg_type type>
struct arg_type_t_impl;

#define CTCLI_DEF_ARG_TYPE_T(arg_t, t)     \
template<>                              \
struct arg_type_t_impl<arg_type::arg_t> \
{ using type = t; }

CTCLI_DEF_ARG_TYPE_T(none, int);

CTCLI_DEF_ARG_TYPE_T(integer,          int);
CTCLI_DEF_ARG_TYPE_T(unsigned_integer, unsigned int);
CTCLI_DEF_ARG_TYPE_T(floating_point,   double);
CTCLI_DEF_ARG_TYPE_T(boolean,          bool);

CTCLI_DEF_ARG_TYPE_T(int8,   std::int8_t);
CTCLI_DEF_ARG_TYPE_T(int16,  std::int16_t);
CTCLI_DEF_ARG_TYPE_T(int32,  std::int32_t);
CTCLI_DEF_ARG_TYPE_T(int64,  std::int64_t);
CTCLI_DEF_ARG_TYPE_T(uint8,  std::uint8_t);
CTCLI_DEF_ARG_TYPE_T(uint16, std::uint16_t);
CTCLI_DEF_ARG_TYPE_T(uint32, std::uint32_t);
CTCLI_DEF_ARG_TYPE_T(uint64, std::uint64_t);
static_assert(sizeof (float)  == 4);
static_assert(sizeof (double) == 8);
CTCLI_DEF_ARG_TYPE_T(float32, float);
CTCLI_DEF_ARG_TYPE_T(float64, double);

CTCLI_DEF_ARG_TYPE_T(string, std_string_view);

#undef CL_DEF_ARG_TYPE_T


} // namespace internal

/// Converts `type` to the corresponding C++ type.
template<arg_type type>
using arg_type_t = typename internal::arg_type_t_impl<type>::type;

namespace internal
{

template<typename Int>
constexpr optional<Int> parse_integer(string_view str) noexcept
{
	static_assert(std::is_integral_v<Int>);
	constexpr auto is_unsigned = std::is_unsigned_v<Int>;
	auto it = str.begin();
	auto const end = str.end();
	if (it == end)
	{
		return {};
	}

	Int result = 0;

	switch (*it)
	{
	case '-':
		if constexpr (is_unsigned)
		{
			return {};
		}
		++it; // '-'
		if (it == end)
		{
			return {};
		}
		for (; it != end; ++it)
		{
			auto const c = *it;
			if (
				!(c >= '0' && c <= '9')
				|| (result < std::numeric_limits<Int>::min() / 10)
			)
			{
				return {};
			}
			result *= 10;

			auto const to_sub = static_cast<Int>(c - '0');
			if (result < std::numeric_limits<Int>::min() + to_sub)
			{
				return {};
			}
			result -= to_sub;
		}
		return result;
	case '+':
		++it;
		if (it == end)
		{
			return {};
		}
		[[fallthrough]];
	case '0': case '1': case '2': case '3': case '4':
	case '5': case '6': case '7': case '8': case '9':
		for (; it != end; ++it)
		{
			auto const c = *it;
			if (
				!(c >= '0' && c <= '9')
				|| (result > std::numeric_limits<Int>::max() / 10)
			)
			{
				return {};
			}
			result *= 10;

			auto const to_add = static_cast<Int>(c - '0');
			if (result > std::numeric_limits<Int>::max() - to_add)
			{
				return {};
			}
			result += to_add;
		}
		return result;
	default:
		return {};
	}
}


template<arg_type type>
struct arg_parser;

#define CTCLI_DEF_ARG_PARSER_INT(arg_t)                                                    \
template<>                                                                                 \
struct arg_parser<arg_type::arg_t>                                                         \
{                                                                                          \
    static constexpr optional<arg_type_t<arg_type::arg_t>> parse(string_view arg) noexcept \
    { return parse_integer<arg_type_t<arg_type::arg_t>>(arg); }                            \
};

CTCLI_DEF_ARG_PARSER_INT(integer)
CTCLI_DEF_ARG_PARSER_INT(unsigned_integer)
CTCLI_DEF_ARG_PARSER_INT(int8)
CTCLI_DEF_ARG_PARSER_INT(int16)
CTCLI_DEF_ARG_PARSER_INT(int32)
CTCLI_DEF_ARG_PARSER_INT(int64)
CTCLI_DEF_ARG_PARSER_INT(uint8)
CTCLI_DEF_ARG_PARSER_INT(uint16)
CTCLI_DEF_ARG_PARSER_INT(uint32)
CTCLI_DEF_ARG_PARSER_INT(uint64)

#undef CTCLI_DEF_ARG_PARSER_INT


template<>
struct arg_parser<arg_type::floating_point>;

template<>
struct arg_parser<arg_type::float32>;

template<>
struct arg_parser<arg_type::float64>;


template<>
struct arg_parser<arg_type::boolean>
{
	static constexpr optional<bool> parse(string_view arg) noexcept
	{
		if (arg == "true")
		{
			return true;
		}
		else if (arg == "false")
		{
			return false;
		}
		else
		{
			return {};
		}
	}
};

template<>
struct arg_parser<arg_type::string>
{
	static constexpr optional<string_view> parse(string_view arg) noexcept
	{
		return arg;
	}
};

} // namespace internal


enum class visibility_kind
{
	visible,
	hidden,
	undocumented,
};


enum class group_id_t : std::uint32_t
{
	invalid = std::uint32_t(-1),
	_0 = 0, _1 = 1, _2 = 2, _3 = 3, _4 = 4,
	_5 = 5, _6 = 6, _7 = 7, _8 = 8, _9 = 9,
};

struct group_element_t
{
	string_view     usage;
	string_view     help;
	arg_type        type;
	visibility_kind visibility;
};

constexpr group_element_t create_group_element(string_view usage, string_view help, arg_type type = arg_type::none)
{
	internal::check_group_elment_syntax(usage);
	assert((internal::is_bool_flag(usage) || type != arg_type::none) && "please provide a ctcli::arg_type for non bool flags");
	return group_element_t{ usage, help, type, visibility_kind::visible };
}

constexpr group_element_t create_hidden_group_element(string_view usage, string_view help, arg_type type = arg_type::none)
{
	internal::check_group_elment_syntax(usage);
	assert((internal::is_bool_flag(usage) || type != arg_type::none) && "please provide a ctcli::arg_type for non bool flags");
	return group_element_t{ usage, help, type, visibility_kind::hidden };
}

constexpr group_element_t create_undocumented_group_element(string_view usage, string_view help, arg_type type = arg_type::none)
{
	internal::check_group_elment_syntax(usage);
	assert((internal::is_bool_flag(usage) || type != arg_type::none) && "please provide a ctcli::arg_type for non bool flags");
	return group_element_t{ usage, help, type, visibility_kind::undocumented };
}


template<group_id_t ID>
inline constexpr char option_group = 0;

template<group_id_t ID>
inline constexpr char option_group_multiple = 0;


/// Whether there's a default help flag provided or not.  Specialize this variable if necessary.
template<group_id_t ID>
inline constexpr bool add_help_to_group = true;

/// The default help group element.  Can be specialized to other flags, e.g. only '--help'.
template<group_id_t ID>
inline constexpr auto help_group_element = create_group_element("help", "Display this help page");


template<std::size_t N>
struct multiple_group_element_t
{
	string_view           usage;
	string_view           help;
	array<std::uint32_t, N> element_indicies;
	visibility_kind       visibility;
};

template<group_id_t ID>
constexpr multiple_group_element_t<option_group<ID>.size()>
create_multiple_group_element(string_view usage, string_view help, std::initializer_list<string_view> elements)
{
	constexpr auto const &group = option_group<ID>;
	assert(elements.size() <= group.size());
	internal::check_group_elment_syntax(usage);
	assert(internal::is_bool_flag(usage));
	auto const indicies = [&elements]() {
		array<std::uint32_t, group.size()> indicies_{};
		for (auto &index : indicies_)
		{
			index = std::numeric_limits<std::uint32_t>::max();
		}
		std::size_t i = 0;
		for (auto const element : elements)
		{
			auto const it = std::find_if(
				group.begin(), group.end(),
				[&element](auto const &group_element) {
					return element == group_element.usage;
				}
			);
			assert(it != group.end());
			assert(internal::is_bool_flag(it->usage));
			indicies_[i] = static_cast<std::uint32_t>(it - group.begin());
			++i;
		}
		return indicies_;
	}();
	return multiple_group_element_t<option_group<ID>.size()>{ usage, help, indicies, visibility_kind::visible };
}

template<group_id_t ID>
constexpr multiple_group_element_t<option_group<ID>.size()>
create_hidden_multiple_group_element(string_view usage, string_view help, std::initializer_list<string_view> elements)
{
	constexpr auto const &group = option_group<ID>;
	assert(elements.size() <= group.size());
	internal::check_group_elment_syntax(usage);
	assert(internal::is_bool_flag(usage));
	auto const indicies = [&elements]() {
		array<std::uint32_t, group.size()> indicies_{};
		for (auto &index : indicies_)
		{
			index = std::numeric_limits<std::uint32_t>::max();
		}
		std::size_t i = 0;
		for (auto const element : elements)
		{
			auto const it = std::find_if(
				group.begin(), group.end(),
				[&element](auto const &group_element) {
					return element == group_element.usage;
				}
			);
			assert(it != group.end());
			assert(internal::is_bool_flag(it->usage));
			indicies_[i] = static_cast<std::uint32_t>(it - group.begin());
			++i;
		}
		return indicies_;
	}();
	return multiple_group_element_t<option_group<ID>.size()>{ usage, help, indicies, visibility_kind::hidden };
}

template<group_id_t ID>
constexpr multiple_group_element_t<option_group<ID>.size()>
create_undocumented_multiple_group_element(string_view usage, string_view help, std::initializer_list<string_view> elements)
{
	constexpr auto const &group = option_group<ID>;
	assert(elements.size() <= group.size());
	internal::check_group_elment_syntax(usage);
	assert(internal::is_bool_flag(usage));
	auto const indicies = [&elements]() {
		array<std::uint32_t, group.size()> indicies_{};
		for (auto &index : indicies_)
		{
			index = std::numeric_limits<std::uint32_t>::max();
		}
		std::size_t i = 0;
		for (auto const element : elements)
		{
			auto const it = std::find_if(
				group.begin(), group.end(),
				[&element](auto const &group_element) {
					return element == group_element.usage;
				}
			);
			assert(it != group.end());
			assert(internal::is_bool_flag(it->usage));
			indicies_[i] = static_cast<std::uint32_t>(it - group.begin());
			++i;
		}
		return indicies_;
	}();
	return multiple_group_element_t<option_group<ID>.size()>{ usage, help, indicies, visibility_kind::undocumented };
}


/// An enum to differentiate between different options arrays.
enum class options_id_t : std::uint32_t
{
	invalid = std::uint32_t(-1) - 1,
	def = std::uint32_t(-1),
	_0 = 0, _1 = 1, _2 = 2, _3 = 3, _4 = 4,
	_5 = 5, _6 = 6, _7 = 7, _8 = 8, _9 = 9,
};

struct option_t
{
	string_view     usage;
	string_view     help;
	arg_type        type;
	group_id_t      group_id;
	string_view     group_name;
	visibility_kind visibility;
};

/// Creates an option with the specified fields.
constexpr option_t create_option(string_view usage, string_view help, arg_type type = arg_type::none)
{
	internal::check_flag_syntax(usage);
	assert((internal::is_bool_flag(usage) || internal::is_choice_value(usage) || type != arg_type::none) && "please provide a ctcli::arg_type for non bool flags");
	return option_t{ usage, help, type, group_id_t::invalid, string_view{}, visibility_kind::visible };
}

/// Creates a hidden option with the specified fields.
/// Hidden options only appear in help strings if verbose output is used.
constexpr option_t create_hidden_option(string_view usage, string_view help, arg_type type = arg_type::none)
{
	internal::check_flag_syntax(usage);
	assert((internal::is_bool_flag(usage) || internal::is_choice_value(usage) || type != arg_type::none) && "please provide a ctcli::arg_type for non bool flags");
	return option_t{ usage, help, type, group_id_t::invalid, string_view{}, visibility_kind::hidden };
}

/// Creates an undocumented option with the specified fields.
/// Undocumented options do not appear in help strings.
constexpr option_t create_undocumented_option(string_view usage, string_view help, arg_type type = arg_type::none)
{
	internal::check_flag_syntax(usage);
	assert((internal::is_bool_flag(usage) || internal::is_choice_value(usage) || type != arg_type::none) && "please provide a ctcli::arg_type for non bool flags");
	return option_t{ usage, help, type, group_id_t::invalid, string_view{}, visibility_kind::undocumented };
}

constexpr option_t create_group_option(string_view usage, string_view help, group_id_t group_id, string_view group_name)
{
	internal::check_flag_syntax(usage);
	return option_t{ usage, help, arg_type::none, group_id, group_name, visibility_kind::visible };
}

constexpr option_t create_hidden_group_option(string_view usage, string_view help, group_id_t group_id, string_view group_name)
{
	internal::check_flag_syntax(usage);
	return option_t{ usage, help, arg_type::none, group_id, group_name, visibility_kind::hidden };
}

constexpr option_t create_undocumented_group_option(string_view usage, string_view help, group_id_t group_id, string_view group_name)
{
	internal::check_flag_syntax(usage);
	return option_t{ usage, help, arg_type::none, group_id, group_name, visibility_kind::undocumented };
}


/// The main options array; specialize this variable to add options.
/// e.g.
/// template<>
/// inline constexpr std::array ctcli::command_line_options<id> = {
///     ctcli::create_option("-o <file>", "Write output to file", ctcli::arg_type::string),
///     // ...
/// };
template<options_id_t ID>
inline constexpr char command_line_options = 0;


/// Whether there's a default help flag provided or not.  Specialize this variable if necessary.
template<options_id_t ID>
inline constexpr bool add_help_option = true;

/// The default help flag.  Can be specialized to other flags, e.g. only '--help'.
template<options_id_t ID>
inline constexpr auto help_option = create_option("-h, --help", "Display this help page");

/// Whether there's a default verbose flag provided or not.  Specialize this variable if necessary.
template<options_id_t ID>
inline constexpr bool add_verbose_option = false;

/// The default verbose flag.  Can be specialized to other flags, e.g. only '-v'.
template<options_id_t ID>
inline constexpr auto verbose_option = create_option("-v, --verbose", "Use verbose output");


enum class commands_id_t : std::uint32_t
{
	def = std::uint32_t(-1),
	_0 = 0, _1 = 1, _2 = 2, _3 = 3, _4 = 4,
	_5 = 5, _6 = 6, _7 = 7, _8 = 8, _9 = 9,
};

struct command_t
{
	string_view     usage;
	string_view     help;
	string_view     positional_names;
	options_id_t    options_id;
	arg_type        type;
	visibility_kind visibility;
};

constexpr command_t create_command(
	string_view usage, string_view help, string_view positional_names,
	options_id_t options_id, arg_type type = arg_type::none
)
{
	internal::check_command_syntax(usage);
	return command_t{ usage, help, positional_names, options_id, type, visibility_kind::visible };
}

constexpr command_t create_hidden_command(
	string_view usage, string_view help, string_view positional_names,
	options_id_t options_id, arg_type type = arg_type::none
)
{
	internal::check_command_syntax(usage);
	return command_t{ usage, help, positional_names, options_id, type, visibility_kind::hidden };
}

constexpr command_t create_undocumented_command(
	string_view usage, string_view help, string_view positional_names,
	options_id_t options_id, arg_type type = arg_type::none
)
{
	internal::check_command_syntax(usage);
	return command_t{ usage, help, positional_names, options_id, type, visibility_kind::undocumented };
}

template<commands_id_t ID>
inline constexpr char command_line_commands = 0;


/// Whether there's a default help command provided or not.  Specialize this variable if necessary.
template<commands_id_t ID>
inline constexpr bool add_help_command = true;

/// The default help command.  Can be specialized to other commands.
template<commands_id_t ID>
inline constexpr auto help_command = create_command("help", "Display this help page", "", options_id_t::invalid);


namespace internal
{

// Helper variables to determine what to parse in parse_command_line.

inline const int default_parse_tag = 0;
template<auto>
static constexpr commands_id_t default_commands_id = commands_id_t::def;
template<auto>
static constexpr options_id_t default_options_id = options_id_t::def;


// Some helper variable templates.

template<typename T, typename U>
static constexpr bool is_array_of_type_v = false;

template<std::size_t N, typename T>
static constexpr bool is_array_of_type_v<array<T, N>, T> = true;

template<group_id_t ID>
constexpr bool is_valid_option_group_type_v = is_array_of_type_v<
	std::remove_cv_t<std::remove_reference_t<decltype(option_group<ID>)>>,
	group_element_t
>;

template<group_id_t ID>
constexpr bool is_valid_option_group_multiple_type_v = is_array_of_type_v<
	std::remove_cv_t<std::remove_reference_t<decltype(option_group_multiple<ID>)>>,
	multiple_group_element_t<option_group<ID>.size()>
>;

template<options_id_t ID>
constexpr bool is_valid_options_type_v = is_array_of_type_v<
	std::remove_cv_t<std::remove_reference_t<decltype(command_line_options<ID>)>>,
	option_t
>;

template<commands_id_t ID>
constexpr bool is_valid_commands_type_v = is_array_of_type_v<
	std::remove_cv_t<std::remove_reference_t<decltype(command_line_commands<ID>)>>,
	command_t
>;


/// Type to represent a unique index for a group element.
/// We use an enum class to have a unique integral type for the index.
/// The first 32 bits are for storing the group_id_t, the rest are for the index of the flag
/// in option_group<ID>.
enum class group_element_index_t : std::uint64_t
{};

/// Type to represent a unique index for an option.
/// We use an enum class to have a unique integral type for the index.
/// The first 32 bits are for storing the options_id_t, the rest are for the index of the flag
/// in command_line_options<ID>.
enum class option_index_t : std::uint64_t
{};

/// Type to represent a unique index for a command.
/// We use an enum class to have a unique integral type for the index.
/// The first 32 bits are for storing the commands_id_t, the rest are for the index of the command
/// in command_line_commands<ID>.
enum class command_index_t : std::uint64_t
{};


constexpr group_element_index_t create_group_element_index(group_id_t id, std::uint32_t index) noexcept
{
	return static_cast<group_element_index_t>(
		(static_cast<std::uint64_t>(id) << 32) | static_cast<std::uint64_t>(index)
	);
}

constexpr option_index_t create_option_index(options_id_t id, std::uint32_t index) noexcept
{
	return static_cast<option_index_t>(
		(static_cast<std::uint64_t>(id) << 32) | static_cast<std::uint64_t>(index)
	);
}

constexpr command_index_t create_command_index(commands_id_t id, std::uint32_t index) noexcept
{
	return static_cast<command_index_t>(
		(static_cast<std::uint64_t>(id) << 32) | static_cast<std::uint64_t>(index)
	);
}

constexpr std::uint32_t get_integer_group_element_index(group_element_index_t index) noexcept
{
	return static_cast<std::uint32_t>(index);
}

constexpr std::uint32_t get_integer_option_index(option_index_t index) noexcept
{
	return static_cast<std::uint32_t>(index);
}

constexpr std::uint32_t get_integer_command_index(command_index_t index) noexcept
{
	return static_cast<std::uint32_t>(index);
}

constexpr group_id_t get_group_id_t(group_element_index_t index) noexcept
{
	return static_cast<group_id_t>(static_cast<std::uint64_t>(index) >> 32);
}

constexpr options_id_t get_options_id_t(option_index_t index) noexcept
{
	return static_cast<options_id_t>(static_cast<std::uint64_t>(index) >> 32);
}

constexpr commands_id_t get_commands_id_t(command_index_t index) noexcept
{
	return static_cast<commands_id_t>(static_cast<std::uint64_t>(index) >> 32);
}

template<group_id_t ID>
static constexpr auto help_group_element_index = (std::enable_if_t<add_help_to_group<ID>, int>{}, create_group_element_index(ID, option_group<ID>.size()));

template<options_id_t ID>
static constexpr auto help_option_index = (std::enable_if_t<add_help_option<ID>, int>{}, create_option_index(ID, command_line_options<ID>.size()));

template<options_id_t ID>
static constexpr auto verbose_option_index = (std::enable_if_t<add_verbose_option<ID>, int>{}, create_option_index(ID, command_line_options<ID>.size() + (add_help_option<ID> ? 1 : 0)));

template<commands_id_t ID>
static constexpr auto help_command_index = create_command_index(ID, command_line_commands<ID>.size());

template<group_element_index_t Index>
constexpr group_element_t const &get_group_element(void) noexcept
{
	constexpr auto integer_index = get_integer_group_element_index(Index);
	constexpr auto id = get_group_id_t(Index);
	if constexpr (integer_index == option_group<id>.size())
	{
		static_assert(add_help_to_group<id>);
		return help_group_element<id>;
	}
	else
	{
		return option_group<id>[integer_index];
	}
}

template<option_index_t Index>
constexpr option_t const &get_option(void) noexcept
{
	constexpr auto integer_index = get_integer_option_index(Index);
	constexpr auto id = get_options_id_t(Index);
	if constexpr (integer_index < command_line_options<id>.size())
	{
		return command_line_options<id>[integer_index];
	}
	else if constexpr (integer_index == command_line_options<id>.size())
	{
		if constexpr (add_help_option<id>)
		{
			return help_option<id>;
		}
		else
		{
			static_assert(add_verbose_option<id>);
			return verbose_option<id>;
		}
	}
	else
	{
		static_assert(integer_index == command_line_options<id>.size() + 1);
		return verbose_option<id>;
	}
}

template<command_index_t Index>
constexpr command_t const &get_command(void) noexcept
{
	constexpr auto integer_index = get_integer_command_index(Index);
	constexpr auto id = get_commands_id_t(Index);
	if constexpr (integer_index == command_line_commands<id>.size())
	{
		static_assert(add_help_command<id>);
		return help_command<id>;
	}
	else
	{
		return command_line_commands<id>[integer_index];
	}
}

template<group_id_t ID>
constexpr group_element_index_t get_group_element_index(string_view flag_name) noexcept
{
	static_assert(is_valid_option_group_type_v<ID>, "The option group array with the given ID isn't specialized or has an invalid type");
	assert(flag_name != "");
	std::uint32_t i = 0;
	for (auto const &group_element : option_group<ID>)
	{
		auto const [first_flag_name, second_flag_name] = get_flag_names(group_element.usage);
		if (first_flag_name == flag_name || second_flag_name == flag_name)
		{
			return create_group_element_index(ID, i);
		}
		++i;
	}
	if constexpr (add_help_to_group<ID>)
	{
		auto const [first_flag_name, second_flag_name] = get_flag_names(help_group_element<ID>.usage);
		if (first_flag_name == flag_name || second_flag_name == flag_name)
		{
			return create_group_element_index(ID, i);
		}
		++i;
	}
	CTCLI_UNREACHABLE;
}

template<options_id_t ID>
constexpr option_index_t get_option_index(string_view flag_name) noexcept
{
	static_assert(is_valid_options_type_v<ID>, "The options array with the given ID isn't specialized or has an invalid type");
	assert(flag_name != "");
	assert(
		(flag_name.starts_with('-') && flag_name.length() == 2)
		|| (flag_name.starts_with("--") && flag_name.length() > 2)
	);
	auto const flag_name_without_dashes = flag_name.length() == 2 ? flag_name.substr(1) : flag_name.substr(2);
	std::uint32_t i = 0;
	for (auto const &option : command_line_options<ID>)
	{
		auto const [first_flag_name, second_flag_name] = get_flag_names(option.usage);
		if (first_flag_name == flag_name_without_dashes || second_flag_name == flag_name_without_dashes)
		{
			return create_option_index(ID, i);
		}
		++i;
	}
	if constexpr (add_help_option<ID>)
	{
		auto const [first_flag_name, second_flag_name] = get_flag_names(help_option<ID>.usage);
		if (first_flag_name == flag_name_without_dashes || second_flag_name == flag_name_without_dashes)
		{
			return create_option_index(ID, i);
		}
		++i;
	}
	if constexpr (add_verbose_option<ID>)
	{
		auto const [first_flag_name, second_flag_name] = get_flag_names(verbose_option<ID>.usage);
		if (first_flag_name == flag_name_without_dashes || second_flag_name == flag_name_without_dashes)
		{
			return create_option_index(ID, i);
		}
		++i;
	}
	CTCLI_UNREACHABLE;
}

constexpr std::pair<string_view, string_view> seperate_option_and_group_element(string_view flag_name)
{
	assert(flag_name.starts_with('-'));
	if (flag_name.starts_with("--"))
	{
		auto const space_it = std::find(flag_name.begin(), flag_name.end(), ' ');
		if (space_it == flag_name.end())
		{
			return { string_view{}, flag_name };
		}
		else
		{
			return { string_view(flag_name.begin(), space_it), string_view(space_it + 1, flag_name.end()) };
		}
	}
	else
	{
		auto const space_it = std::find(flag_name.begin(), flag_name.end(), ' ');
		if (space_it == flag_name.end())
		{
			auto const option_name = flag_name.substr(0, 2);
			auto const group_element_name = flag_name.substr(2);
			return { option_name, group_element_name };
		}
		else
		{
			return { string_view(flag_name.begin(), space_it), string_view(space_it + 1, flag_name.end()) };
		}
	}
}

constexpr std::pair<string_view, string_view> seperate_command_and_option(string_view flag_name)
{
#if __cplusplus < 202002L
	auto const space_it = [&]() {
		auto it = flag_name.begin();
		auto const end = flag_name.end();
		for (; it != end; ++it)
		{
			if (*it == ' ')
			{
				return it;
			}
		}
		return end;
	}();
#else
	auto const space_it = std::find(flag_name.begin(), flag_name.end(), ' ');
#endif // c++20
	if (space_it == flag_name.end())
	{
		return { string_view{}, flag_name };
	}
	else
	{
		return { string_view(flag_name.begin(), space_it), string_view(space_it + 1, flag_name.end()) };
	}
}

} // namespace internal

template<auto ID = &internal::default_parse_tag>
constexpr internal::group_element_index_t group_element(string_view flag_name)
{
	if constexpr (std::is_same_v<decltype(ID), group_id_t>)
	{
		return internal::get_group_element_index<ID>(flag_name);
	}
	else if constexpr (std::is_same_v<decltype(ID), options_id_t>)
	{
		auto const option_and_group_element = internal::seperate_option_and_group_element(flag_name);
		assert(option_and_group_element.first  != "");
		assert(option_and_group_element.second != "");
		return [&]<std::size_t ...Is>(std::index_sequence<Is...>) {
			bool is_result_set = false;
			internal::group_element_index_t result{};
			(([&]() {
				constexpr auto const &option = internal::get_option<internal::create_option_index(ID, static_cast<std::uint32_t>(Is))>();
				constexpr auto option_names = internal::get_flag_names_with_dashes(option.usage);
				if constexpr (option.group_id != group_id_t::invalid)
				{
					if (
						option_names.first == option_and_group_element.first
						|| option_names.second == option_and_group_element.first
					)
					{
						is_result_set = true;
						result = internal::get_group_element_index<option.group_id>(option_and_group_element.second);
					}
				}
			}()), ...);
			assert(is_result_set);
			return result;
		}(std::make_index_sequence<
			command_line_options<ID>.size()
			+ (add_help_option<ID> ? 1 : 0)
			+ (add_verbose_option<ID> ? 1 : 0)
		>{});
	}
	else if constexpr (std::is_same_v<decltype(ID), commands_id_t>)
	{
		auto const command_and_option_and_group_element = internal::seperate_command_and_option(flag_name);
		assert(command_and_option_and_group_element.first  != "");
		assert(command_and_option_and_group_element.second != "");
		return [&]<std::size_t ...Is>(std::index_sequence<Is...>) {
			bool is_result_set = false;
			internal::group_element_index_t result{};
			(([&]() {
				constexpr auto const &command = internal::get_command<internal::create_command_index(ID, static_cast<std::uint32_t>(Is))>();
				constexpr auto command_names = internal::get_flag_names(command.usage);
				if (
					command_names.first == command_and_option_and_group_element.first
					|| command_names.second == command_and_option_and_group_element.first
				)
				{
					is_result_set = true;
					result = group_element<command.options_id>(command_and_option_and_group_element.second);
				}
			}()), ...);
			assert(is_result_set);
			return result;
		}(std::make_index_sequence<command_line_commands<ID>.size()>{});
	}
	else
	{
		static_assert(ID == &internal::default_parse_tag, "invalid template parameter to ctcli::option");
		static_assert(
			internal::is_valid_commands_type_v<internal::default_commands_id<ID>>
			|| internal::is_valid_options_type_v<internal::default_options_id<ID>>,
			"no default commands or options found"
		);
		if constexpr (internal::is_valid_commands_type_v<internal::default_commands_id<ID>>)
		{
			return group_element<internal::default_commands_id<ID>>(flag_name);
		}
		else
		{
			return group_element<internal::default_options_id<ID>>(flag_name);
		}
	}
}

/// Returns the unique option index associated with `flag_name`.
template<auto ID = &internal::default_parse_tag>
constexpr internal::option_index_t option(string_view flag_name)
{
	if constexpr (std::is_same_v<decltype(ID), options_id_t>)
	{
		return internal::get_option_index<ID>(flag_name);
	}
	else if constexpr (std::is_same_v<decltype(ID), commands_id_t>)
	{
		auto const command_and_option = internal::seperate_command_and_option(flag_name);
		assert(command_and_option.first != "");
		return [&]<std::size_t ...Is>(std::index_sequence<Is...>) {
			bool is_result_set = false;
			internal::option_index_t result{};
			(([&]() {
				constexpr auto const &command = internal::get_command<internal::create_command_index(ID, static_cast<std::uint32_t>(Is))>();
				constexpr auto command_names = internal::get_flag_names(command.usage);
				if (
					command_names.first == command_and_option.first
					|| command_names.second == command_and_option.first
				)
				{
					is_result_set = true;
					result = internal::get_option_index<command.options_id>(command_and_option.second);
				}
			}()), ...);
			assert(is_result_set);
			return result;
		}(std::make_index_sequence<command_line_commands<ID>.size()>{});
	}
	else
	{
		static_assert(ID == &internal::default_parse_tag, "invalid template parameter to ctcli::option");
		static_assert(
			internal::is_valid_commands_type_v<internal::default_commands_id<ID>>
			|| internal::is_valid_options_type_v<internal::default_options_id<ID>>,
			"no default commands or options found"
		);
		if constexpr (internal::is_valid_commands_type_v<internal::default_commands_id<ID>>)
		{
			return option<internal::default_commands_id<ID>>(flag_name);
		}
		else
		{
			return option<internal::default_options_id<ID>>(flag_name);
		}
	}
}

/// Returns the unique option index associated with `flag_name`.
template<commands_id_t ID = commands_id_t::def>
constexpr internal::command_index_t command(string_view command_name)
{
	return [&]<std::size_t ...Is>(std::index_sequence<Is...>) {
		internal::command_index_t result{};
		(( [&]() {
			constexpr auto index = internal::create_command_index(ID, static_cast<std::uint32_t>(Is));
			constexpr auto const &command = internal::get_command<index>();
			constexpr auto command_names = internal::get_flag_names(command.usage);
			if (
				command_names.first == command_name
				|| command_names.second == command_name
			)
			{
				result = index;
			}
		}()), ...);
		return result;
	}(std::make_index_sequence<command_line_commands<ID>.size() + (add_help_command<ID> ? 1 : 0)>{});
}


/// Specialize this variable to use a custom argument parser function.
/// This is required for choice arguments ({val1|val2|val3}), and is forbidden for bool flags.
template<auto Index, std::enable_if_t<
	std::is_same_v<decltype(Index), internal::group_element_index_t>
	|| std::is_same_v<decltype(Index), internal::option_index_t>
	|| std::is_same_v<decltype(Index), internal::command_index_t>,
	int
> = 0>
inline constexpr char argument_parse_function = 0;

/// Specialize this variable to make the option array-like, meaning it can appear multiple
/// times on the command line.  These arguments must not be simple bool flags.
template<auto Index, std::enable_if_t<
	std::is_same_v<decltype(Index), internal::group_element_index_t>
	|| std::is_same_v<decltype(Index), internal::option_index_t>,
	int
> = 0>
inline constexpr bool is_array_like = false;

namespace internal
{

// some helper variables/types to verify specializations of `ctcli::argument_parse_function`

template<typename T>
static constexpr bool is_optional_v = false;

template<typename T>
static constexpr bool is_optional_v<optional<T>> = true;

struct is_valid_argument_parse_function
{
	using yes = int;
	using no = unsigned int;
	template<auto Index, typename ...Args>
	static auto test(Args ...)
	noexcept(is_optional_v<decltype(argument_parse_function<Index>(std::declval<string_view>()))>)
	-> decltype(
		argument_parse_function<Index>(std::declval<string_view>()),
		yes{}
	);

	template<auto Index>
	static auto test(...) noexcept(false) -> no;

	template<auto Index>
	static constexpr bool value =
		std::is_same_v<decltype(test<Index>(42)), yes>
		&& noexcept(test<Index>(42));
};

template<auto Index>
constexpr bool has_argument_parse_function = is_valid_argument_parse_function::value<Index>;


/// Meta type to get the value type of an option.
template<typename T>
struct optional_value_type
{ using type = int; };

template<typename T>
struct optional_value_type<optional<T>>
{ using type = T; };


template<bool is_bool_flag, bool has_argument_parser, group_element_index_t Index>
struct group_element_value_type_impl;

template<group_element_index_t Index>
struct group_element_value_type_impl<true, false, Index>
{ using type = bool; };

template<group_element_index_t Index>
struct group_element_value_type_impl<false, true, Index>
{
	using type = typename optional_value_type<decltype(
		argument_parse_function<Index>(std::declval<string_view>())
	)>::type;
};

template<group_element_index_t Index>
struct group_element_value_type_impl<false, false, Index>
{
	static_assert(get_group_element<Index>().type != arg_type::none);
	using type = arg_type_t<get_group_element<Index>().type>;
};


/// Meta type to get the value type of a group element.
template<group_element_index_t Index>
struct group_element_value_type
{
	using value_type = typename group_element_value_type_impl<
		// special case for help group element
		get_integer_group_element_index(Index) == option_group<get_group_id_t(Index)>.size()
		|| is_bool_flag(get_group_element<Index>().usage),
		has_argument_parse_function<Index>,
		Index
	>::type;
	using type = std::conditional_t<is_array_like<Index>, vector<value_type>, value_type>;
};


template<bool is_bool_flag, bool has_argument_parser, bool is_group, option_index_t Index>
struct option_value_type_impl;

template<option_index_t Index>
struct option_value_type_impl<true, false, false, Index>
{ using type = bool; };

template<option_index_t Index>
struct option_value_type_impl<false, true, false, Index>
{
	using type = typename optional_value_type<decltype(
		argument_parse_function<Index>(std::declval<string_view>())
	)>::type;
};

template<option_index_t Index>
struct option_value_type_impl<false, false, false, Index>
{
	static_assert(get_option<Index>().type != arg_type::none);
	using type = arg_type_t<get_option<Index>().type>;
};

template<bool is_bool_flag, bool has_argument_parser, option_index_t Index>
struct option_value_type_impl<is_bool_flag, has_argument_parser, true, Index>
{
	// a dummy type
	using type = int;
};


/// Meta type to get the value type of an option.
template<option_index_t Index>
struct option_value_type
{
	using value_type = typename option_value_type_impl<
		// special case for help and verbose flags
		get_integer_option_index(Index) >= command_line_options<get_options_id_t(Index)>.size()
		|| is_bool_flag(get_option<Index>().usage),
		has_argument_parse_function<Index>,
		get_option<Index>().group_id != group_id_t::invalid,
		Index
	>::type;
	using type = std::conditional_t<is_array_like<Index>, vector<value_type>, value_type>;
};


template<bool is_bool_flag, bool has_argument_parser, command_index_t Index>
struct command_value_type_impl;

template<command_index_t Index>
struct command_value_type_impl<true, false, Index>
{ using type = bool; };

template<command_index_t Index>
struct command_value_type_impl<false, true, Index>
{
	using type = typename optional_value_type<decltype(
		argument_parse_function<Index>(std::declval<string_view>())
	)>::type;
};

template<command_index_t Index>
struct command_value_type_impl<false, false, Index>
{
	static_assert(get_command<Index>().type != arg_type::none);
	using type = arg_type_t<get_command<Index>().type>;
};


/// Meta type to get the value type of a command.
template<command_index_t Index>
struct command_value_type
{
	using type = typename command_value_type_impl<
		// special case for help command
		get_integer_command_index(Index) == command_line_commands<get_commands_id_t(Index)>.size()
		|| is_bool_flag(get_command<Index>().usage),
		has_argument_parse_function<Index>,
		Index
	>::type;
};


/// The type, where options values are stored alongside its
/// argument index and flag and argument values.
template<typename T>
struct group_element_value_storage_t
{
	T               element_value{};
	std::size_t     flag_position{0};
	std_string_view group_flag_value{};
	std_string_view flag_value{};
	std_string_view arg_value{};
};

/// The type, where options values are stored alongside its
/// argument index and flag and argument values.
template<typename T>
struct option_value_storage_t
{
	T               option_value{};
	std::size_t     flag_position{0};
	std_string_view flag_value{};
	std_string_view arg_value{};
};

constexpr optional<string_view> get_default_value_arg(string_view help) noexcept
{
	auto const open_paren  = help.rfind('(');
	auto const close_paren = help.rfind(')');
	if (
		open_paren == string_view::npos
		|| close_paren == string_view::npos
		|| close_paren != help.length() - 1
	)
	{
		return {};
	}

	auto const inside_parens = help.substr(open_paren);
	if (!inside_parens.starts_with("(default="))
	{
		return {};
	}

	constexpr auto default_len = string_view("(default=").length();
	return inside_parens.substr(default_len, inside_parens.length() - default_len - 1);
}

template<group_element_index_t Index>
inline group_element_value_storage_t<typename group_element_value_type<Index>::type> get_default_group_element_value(void)
{
	using group_element_type = typename group_element_value_type<Index>::type;
	using result_t = group_element_value_storage_t<group_element_type>;
	if constexpr (is_bool_flag(get_group_element<Index>().usage))
	{
		return result_t{ false, 0, string_view{}, string_view{}, string_view{} };
	}
	else if constexpr (
		constexpr auto arg_opt = get_default_value_arg(get_group_element<Index>().help);
		arg_opt.has_value()
	)
	{
		constexpr auto arg = arg_opt.value();
		if constexpr (has_argument_parse_function<Index>)
		{
			auto result = argument_parse_function<Index>(arg);
			if (!result.has_value())
			{
				CTCLI_THROW(std::runtime_error(fmt::format(
					"Failed initializing group element '{}' with the default value of '{}'\n",
					get_group_element<Index>().usage, arg
				)));
			}
			return result_t{ std::move(result.value()), 0, string_view{}, string_view{}, string_view{} };
		}
		else
		{
			auto result = arg_parser<get_group_element<Index>().type>::parse(arg);
			if (!result.has_value())
			{
				CTCLI_THROW(std::runtime_error(fmt::format(
					"Failed initializing group element '{}' with the default value of '{}'\n",
					get_group_element<Index>().usage, arg
				)));
			}
			return result_t{ std::move(result.value()), 0, string_view{}, string_view{}, string_view{} };
		}
	}
	else
	{
		return result_t{ group_element_type{}, 0, string_view{}, string_view{}, string_view{} };
	}
}

template<option_index_t Index>
inline option_value_storage_t<typename option_value_type<Index>::type> get_default_option_value(void)
{
	using option_type = typename option_value_type<Index>::type;
	using result_t = option_value_storage_t<option_type>;
	if constexpr (is_bool_flag(get_option<Index>().usage))
	{
		return result_t{ false, 0, string_view{}, string_view{} };
	}
	else if constexpr (
		constexpr auto arg_opt = get_default_value_arg(get_option<Index>().help);
		arg_opt.has_value()
	)
	{
		constexpr auto arg = arg_opt.value();
		if constexpr (has_argument_parse_function<Index>)
		{
			auto result = argument_parse_function<Index>(arg);
			if (!result.has_value())
			{
				CTCLI_THROW(std::runtime_error(fmt::format(
					"Failed initializing option '{}' with the default value of '{}'\n",
					get_option<Index>().usage, arg
				)));
			}
			return result_t{ std::move(result.value()), 0, string_view{}, string_view{} };
		}
		else
		{
			auto result = arg_parser<get_option<Index>().type>::parse(arg);
			if (!result.has_value())
			{
				CTCLI_THROW(std::runtime_error(fmt::format(
					"Failed initializing option '{}' with the default value of '{}'\n",
					get_option<Index>().usage, arg
				)));
			}
			return result_t{ std::move(result.value()), 0, string_view{}, string_view{} };
		}
	}
	else
	{
		return result_t{ option_type{}, 0, string_view{}, string_view{} };
	}
}

/// Helper function to get the type of the options tuple.
template<group_id_t ID, std::size_t ...Is>
inline auto get_default_group_element_values(std::index_sequence<Is...>)
	-> std::tuple<group_element_value_storage_t<
		typename group_element_value_type<create_group_element_index(ID, static_cast<std::uint32_t>(Is))>::type
	>...>
{
	using result_t = std::tuple<group_element_value_storage_t<
		typename group_element_value_type<create_group_element_index(ID, static_cast<std::uint32_t>(Is))>::type
	>...>;
	return result_t{ get_default_group_element_value<create_group_element_index(ID, static_cast<std::uint32_t>(Is))>()... };
}

/// Helper function to get the type of the options tuple.
template<options_id_t ID, std::size_t ...Is>
inline auto get_default_option_values(std::index_sequence<Is...>)
	-> std::tuple<option_value_storage_t<
		typename option_value_type<create_option_index(ID, static_cast<std::uint32_t>(Is))>::type
	>...>
{
	using result_t = std::tuple<option_value_storage_t<
		typename option_value_type<create_option_index(ID, static_cast<std::uint32_t>(Is))>::type
	>...>;
	return result_t{ get_default_option_value<create_option_index(ID, static_cast<std::uint32_t>(Is))>()... };
}

template<commands_id_t ID, std::size_t ...Is>
inline auto get_default_command_values(std::index_sequence<Is...>)
	-> std::tuple<option_value_storage_t<
		typename command_value_type<create_command_index(ID, static_cast<std::uint32_t>(Is))>::type
	>...>
{
	using result_t = std::tuple<option_value_storage_t<
		typename command_value_type<create_command_index(ID, static_cast<std::uint32_t>(Is))>::type
	>...>;
	return result_t{};
}


/// The global variable that stores the option group values.
template<group_id_t ID>
inline auto group_element_values = get_default_group_element_values<ID>(
	std::make_index_sequence<
		option_group<ID>.size()
		+ (add_help_to_group<ID> ? 1 : 0)
	>{}
);

/// The global variable that stores the option values.
template<options_id_t ID>
inline auto option_values = get_default_option_values<ID>(
	std::make_index_sequence<
		command_line_options<ID>.size()
		+ (add_help_option<ID>    ? 1 : 0)
		+ (add_verbose_option<ID> ? 1 : 0)
	>{}
);

/// The global variable that stores the positional arguments for an options group
template<options_id_t ID>
inline vector<string_view> positional_arguments_storage{};

/// The global variable that stores the command values.
template<commands_id_t ID>
inline auto command_values = get_default_command_values<ID>(
	std::make_index_sequence<command_line_commands<ID>.size() + (add_help_command<ID> ? 1 : 0)>{}
);

/// Accessor for the group element values by index.
template<group_element_index_t Index>
static constexpr auto *group_element_value_storage_ptr = &std::get<get_integer_group_element_index(Index)>(group_element_values<get_group_id_t(Index)>);

/// Accessor for the option values by index.
template<option_index_t Index>
static constexpr auto *option_value_storage_ptr = &std::get<get_integer_option_index(Index)>(option_values<get_options_id_t(Index)>);

/// Accessor for the command values by index.
template<command_index_t Index>
static constexpr auto *command_value_storage_ptr = &std::get<get_integer_command_index(Index)>(command_values<get_commands_id_t(Index)>);

} // namespace internal

#if __cplusplus >= 202002L

namespace internal
{

struct group_element_index_t_
{
	constexpr group_element_index_t_(group_element_index_t _index)
		: index(_index)
	{}

	// template is needed to not instantiate option
	template<auto ID = &internal::default_parse_tag>
	constexpr group_element_index_t_(string_view flag_name)
		: index(group_element<ID>(flag_name))
	{}

	// template is needed to not instantiate option
	template<auto ID = &internal::default_parse_tag>
	constexpr group_element_index_t_(char const *flag_name)
		: index(group_element<ID>(flag_name))
	{}

	group_element_index_t index;
};

} // namespace internal

/// Accessor for the specified option's value.
template<internal::group_element_index_t_ Index>
static auto &group_element_value = internal::group_element_value_storage_ptr<Index.index>->element_value;

/// Accessor for the specified option's index.
template<internal::group_element_index_t_ Index>
static auto &group_element_index = internal::group_element_value_storage_ptr<Index.index>->flag_position;

/// Accessor for the specified option's flag's value.
template<internal::group_element_index_t_ Index>
static auto &group_element_flag_value = internal::group_element_value_storage_ptr<Index.index>->flag_value;

/// Accessor for the specified option's argument (if there's any).
template<internal::group_element_index_t_ Index>
static auto &group_element_arg_value = internal::group_element_value_storage_ptr<Index.index>->arg_value;

/// Returns whether the specified option has been set or not
template<internal::group_element_index_t_ Index>
inline bool is_group_element_set(void) noexcept
{
	return group_element_index<Index> != 0;
}

#else

/// Accessor for the specified option's value.
template<internal::group_element_index_t Index>
static auto &group_element_value = internal::group_element_value_storage_ptr<Index>->element_value;

/// Accessor for the specified option's index.
template<internal::group_element_index_t Index>
static auto &group_element_index = internal::group_element_value_storage_ptr<Index>->flag_position;

/// Accessor for the specified option's flag's value.
template<internal::group_element_index_t Index>
static auto &group_element_flag_value = internal::group_element_value_storage_ptr<Index>->flag_value;

/// Accessor for the specified option's argument (if there's any).
template<internal::group_element_index_t Index>
static auto &group_element_arg_value = internal::group_element_value_storage_ptr<Index>->arg_value;

/// Returns whether the specified option has been set or not
template<internal::group_element_index_t Index>
inline bool is_group_element_set(void) noexcept
{
	return group_element_index<Index> != 0;
}

#endif // c++20


#if __cplusplus >= 202002L

namespace internal
{

struct option_index_t_
{
	constexpr option_index_t_(option_index_t _index)
		: index(_index)
	{}

	// template is needed to not instantiate option
	template<auto ID = &internal::default_parse_tag>
	constexpr option_index_t_(string_view flag_name)
		: index(option<ID>(flag_name))
	{}

	// template is needed to not instantiate option
	template<auto ID = &internal::default_parse_tag>
	constexpr option_index_t_(char const *flag_name)
		: index(option<ID>(flag_name))
	{}

	option_index_t index;
};

} // namespace internal

/// Accessor for the specified option's value.
template<internal::option_index_t_ Index>
static auto &option_value = internal::option_value_storage_ptr<Index.index>->option_value;

/// Accessor for the specified option's index.
template<internal::option_index_t_ Index>
static auto &option_index = internal::option_value_storage_ptr<Index.index>->flag_position;

/// Accessor for the specified option's flag's value.
template<internal::option_index_t_ Index>
static auto &option_flag_value = internal::option_value_storage_ptr<Index.index>->flag_value;

/// Accessor for the specified option's argument (if there's any).
template<internal::option_index_t_ Index>
static auto &option_arg_value = internal::option_value_storage_ptr<Index.index>->arg_value;

/// Returns whether the specified option has been set or not
template<internal::option_index_t_ Index>
inline bool is_option_set(void) noexcept
{
	return option_index<Index> != 0;
}

#else

/// Accessor for the specified option's value.
template<internal::option_index_t Index>
static auto &option_value = internal::option_value_storage_ptr<Index>->option_value;

/// Accessor for the specified option's index.
template<internal::option_index_t Index>
static auto &option_index = internal::option_value_storage_ptr<Index>->flag_position;

/// Accessor for the specified option's flag's value.
template<internal::option_index_t Index>
static auto &option_flag_value = internal::option_value_storage_ptr<Index>->flag_value;

/// Accessor for the specified option's argument (if there's any).
template<internal::option_index_t Index>
static auto &option_arg_value = internal::option_value_storage_ptr<Index>->arg_value;

/// Returns whether the specified option has been set or not
template<internal::option_index_t Index>
inline bool is_option_set(void) noexcept
{
	return option_index<Index> != 0;
}

#endif // c++20


#if __cplusplus >= 202002L

namespace internal
{

struct command_index_t_
{
	constexpr command_index_t_(command_index_t _index)
		: index(_index)
	{}

	// template is needed to not instantiate option
	template<commands_id_t ID = commands_id_t::def>
	constexpr command_index_t_(string_view flag_name)
		: index(command<ID>(flag_name))
	{}

	// template is needed to not instantiate option
	template<commands_id_t ID = commands_id_t::def>
	constexpr command_index_t_(char const *flag_name)
		: index(command<ID>(flag_name))
	{}

	command_index_t index;
};

} // namespace internal

/// Accessor for the specified command's value.
template<internal::command_index_t_ Index>
static auto &command_value = internal::command_value_storage_ptr<Index.index>->option_value;

/// Accessor for the specified command's index.
template<internal::command_index_t_ Index>
static auto &command_index = internal::command_value_storage_ptr<Index.index>->flag_position;

/// Accessor for the specified command's flag's value.
template<internal::command_index_t_ Index>
static auto &command_flag_value = internal::command_value_storage_ptr<Index.index>->flag_value;

/// Accessor for the specified command's argument (if there's any).
template<internal::command_index_t_ Index>
static auto &command_arg_value = internal::command_value_storage_ptr<Index.index>->arg_value;

/// Returns whether the specified command has been set or not
template<internal::command_index_t_ Index>
inline bool is_command_set(void) noexcept
{
	return command_index<Index> != 0;
}

#else

/// Accessor for the specified command's value.
template<internal::command_index_t Index>
static auto &command_value = internal::command_value_storage_ptr<Index>->option_value;

/// Accessor for the specified command's index.
template<internal::command_index_t Index>
static auto &command_index = internal::command_value_storage_ptr<Index>->flag_position;

/// Accessor for the specified command's flag's value.
template<internal::command_index_t Index>
static auto &command_flag_value = internal::command_value_storage_ptr<Index>->flag_value;

/// Accessor for the specified command's argument (if there's any).
template<internal::command_index_t Index>
static auto &command_arg_value = internal::command_value_storage_ptr<Index>->arg_value;

/// Returns whether the specified command has been set or not
template<internal::command_index_t Index>
inline bool is_command_set(void) noexcept
{
	return command_index<Index> != 0;
}

#endif // c++20

/// Accessor for the positional arguments for the given options
template<auto ID>
static auto &positional_arguments = []() -> auto & {
	if constexpr (std::is_same_v<decltype(ID), options_id_t>)
	{
		return internal::positional_arguments_storage<ID>;
	}
	else if constexpr (std::is_same_v<decltype(ID), internal::command_index_t>)
	{
		return internal::positional_arguments_storage<internal::get_command<ID>().options_id>;
	}
	else
	{
		static_assert(std::is_same_v<decltype(ID), options_id_t> || std::is_same_v<decltype(ID), internal::command_index_t>, "invalid template argument to ctcli::positional_arguments");
	}
}();


namespace internal
{

inline vector<string_view> create_args_vector(int argc, char const **argv)
{
	vector<string_view> args;
	args.reserve(static_cast<std::size_t>(argc));
	for (int i = 0; i < argc; ++i)
	{
		args.emplace_back(argv[i]);
	}
	return args;
}

using iter_t = vector<string_view>::const_iterator;

template<option_index_t Index>
bool try_parse_bool_flag(
	string_view option_value, std::size_t flag_position, bool is_single_char,
	string &error
)
{
	static_assert(is_valid_options_type_v<get_options_id_t(Index)>, "The options array with the given ID isn't specialized or has an invalid type");
	constexpr auto usage = get_option<Index>().usage;
	constexpr auto option_names = get_flag_names(usage);
	constexpr auto option_names_with_dashes = get_flag_names_with_dashes(usage);
	constexpr bool has_first_option_name  = option_names.first  != "";
	constexpr bool has_second_option_name = option_names.second != "";

	auto const matched_arg_num =
		( is_single_char && has_first_option_name  && option_value == option_names.first)  ? 0 :
		(!is_single_char && has_second_option_name && option_value == option_names.second) ? 1 :
		-1;

	if (matched_arg_num != -1)
	{
		constexpr auto *flag = option_value_storage_ptr<Index>;
		auto const matched_option_name = matched_arg_num == 0
			? option_names_with_dashes.first
			: option_names_with_dashes.second;

		if (flag->flag_position == 0)
		{
			flag->option_value  = true;
			flag->flag_position = flag_position;
			flag->flag_value    = matched_option_name;
		}
		else
		{
			error = fmt::format(
				"option '{}' has already been set by argument '{}' at position {}",
				matched_option_name, flag->flag_value, flag->flag_position
			);
		}
		return true;
	}
	else
	{
		return false;
	}
}

template<group_element_index_t Index>
bool try_parse_group_element(
	string_view option_group_flag_value, string_view element_value, optional<string_view> arg_opt,
	std::size_t flag_position, string &error
)
{
	static_assert(is_valid_option_group_type_v<get_group_id_t(Index)>, "The option_group array with the give ID isn't specialized or has an invalid type");
	constexpr auto usage = get_group_element<Index>().usage;
	constexpr auto element_names = get_flag_names(usage);
	constexpr auto has_first_element_name  = element_names.first  != "";
	constexpr auto has_second_element_name = element_names.second != "";
	constexpr auto is_bool_flag_ = is_bool_flag(usage);

	auto const original_element_value = element_value;
	auto const is_negation = is_bool_flag_ && element_value.starts_with("no-");
	if (is_bool_flag_ && is_negation)
	{
		element_value = element_value.substr(3);
	}

	auto const matched_arg_num =
		(has_first_element_name  && element_value == element_names.first)  ? 0 :
		(has_second_element_name && element_value == element_names.second) ? 1 :
		-1;

	if (matched_arg_num != -1)
	{
		if constexpr (is_bool_flag(usage))
		{
			if (arg_opt.has_value())
			{
				error = fmt::format("no argument expected for option group flag '{} {}'", option_group_flag_value, element_value);
				return true;
			}

			constexpr auto *flag = group_element_value_storage_ptr<Index>;
			if (flag->flag_position == 0)
			{
				flag->element_value = is_negation ? false : true;
				flag->flag_position = flag_position;
				flag->group_flag_value = option_group_flag_value;
				flag->flag_value = original_element_value;
			}
			else
			{
				error = fmt::format(
					"option group flag '{} {}' has already been set by argument '{} {}' at position {}",
					option_group_flag_value, element_value,
					flag->group_flag_value, flag->flag_value,
					flag->flag_position
				);
				return true;
			}
		}
		else
		{
			static_assert(is_equals_flag(usage));
			if (!arg_opt.has_value())
			{
				error = fmt::format("expected an argument for option group flag '{} {}'", option_group_flag_value, element_value);
				return true;
			}

			auto const arg = arg_opt.value();
			constexpr auto *flag = group_element_value_storage_ptr<Index>;
			if (is_array_like<Index> || flag->flag_position == 0)
			{
				flag->flag_position = flag_position;
				flag->group_flag_value = option_group_flag_value;
				flag->flag_value = original_element_value;
				flag->arg_value = arg;
				if constexpr (has_argument_parse_function<Index>)
				{
					auto result = argument_parse_function<Index>(arg);
					if (result.has_value())
					{
						if constexpr (is_array_like<Index>)
						{
							flag->element_value.emplace_back(std::move(result.value()));
						}
						else
						{
							flag->element_value = std::move(result.value());
						}
					}
					else
					{
						error = fmt::format("invalid argument '{}' for option group flag '{} {}'", arg, option_group_flag_value, element_value);
						return true;
					}
				}
				else
				{
					auto result = arg_parser<get_group_element<Index>().type>::parse(arg);
					if (result.has_value())
					{
						if constexpr (is_array_like<Index>)
						{
							flag->element_value.emplace_back(std::move(result.value()));
						}
						else
						{
							flag->element_value = std::move(result.value());
						}
					}
					else
					{
						error = fmt::format("invalid argument '{}' for option group flag '{} {}'", arg, option_group_flag_value, element_value);
						return true;
					}
				}
			}
			else
			{
				error = fmt::format(
					"option group flag '{} {}' has already been set by argument '{} {}' with the value '{}' at position {}",
					option_group_flag_value, element_value,
					flag->group_flag_value, flag->flag_value, flag->arg_value,
					flag->flag_position
				);
				return true;
			}
		}
		return true;
	}
	else
	{
		return false;
	}
}

template<group_id_t ID, std::size_t N>
bool try_parse_multiple_group_element(
	string_view option_group_flag_value, string_view element_value, optional<string_view> arg_opt,
	string &error
)
{
	static_assert(is_valid_option_group_type_v<ID>, "The option_group array with the give ID isn't specialized or has an invalid type");
	static_assert(is_valid_option_group_multiple_type_v<ID>, "The option_group_multiple array with the give ID isn't specialized or has an invalid type");
	constexpr auto const &multiple_group_element = option_group_multiple<ID>[N];
	constexpr auto usage = multiple_group_element.usage;
	constexpr auto element_names = get_flag_names(usage);
	constexpr auto has_first_element_name  = element_names.first  != "";
	constexpr auto has_second_element_name = element_names.second != "";
	static_assert(is_bool_flag(usage));

	auto const matched_arg_num =
		(has_first_element_name  && element_value == element_names.first)  ? 0 :
		(has_second_element_name && element_value == element_names.second) ? 1 :
		-1;

	if (matched_arg_num != -1)
	{
		if (arg_opt.has_value())
		{
			error = fmt::format("no argument expected for option group flag '{} {}'", option_group_flag_value, element_value);
			return true;
		}

		[&]<std::size_t ...Is>(std::index_sequence<Is...>) {
			(([&]() {
				constexpr auto index = multiple_group_element.element_indicies[Is];
				if constexpr (index != std::numeric_limits<std::uint32_t>::max())
				{
					constexpr auto *flag = group_element_value_storage_ptr<create_group_element_index(ID, index)>;
					if (flag->flag_position == 0)
					{
						flag->element_value = true;
					}
				}
			}()), ...);
		}(std::make_index_sequence<multiple_group_element.element_indicies.size()>{});
		return true;
	}
	else
	{
		return false;
	}
}

template<option_index_t Index, typename ArgFn>
bool try_parse_group_flag(
	string_view option_value, std::size_t flag_position, bool is_single_char,
	string &error, ArgFn get_arg_fn
)
{
	static_assert(is_valid_options_type_v<get_options_id_t(Index)>, "The options array with the given ID isn't specialized or has an invalid type");
	constexpr auto usage = get_option<Index>().usage;
	constexpr auto option_names = get_flag_names(usage);
	constexpr auto option_names_with_dashes = get_flag_names_with_dashes(usage);
	constexpr bool has_first_option_name  = option_names.first  != "";
	constexpr bool has_second_option_name = option_names.second != "";

	auto const matched_arg_num =
		( is_single_char && has_first_option_name  && option_value == option_names.first)  ? 0 :
		(!is_single_char && has_second_option_name && option_value == option_names.second) ? 1 :
		-1;

	if (matched_arg_num != -1)
	{
		auto const matched_option_name = matched_arg_num == 0
			? option_names_with_dashes.first
			: option_names_with_dashes.second;
		auto const arg_opt = get_arg_fn();
		if (!arg_opt.has_value())
		{
			error = fmt::format("expected an argument for option '{}'", matched_option_name);
			return true;
		}
		auto const arg = arg_opt.value();
		auto const eq_it = std::find(arg.begin(), arg.end(), '=');
		auto const arg_option_value = string_view(arg.begin(), eq_it);
		auto const arg_arg_value = eq_it == arg.end()
			? optional<string_view>()
			: optional<string_view>(string_view(eq_it + 1, arg.end()));

		constexpr auto group_id = get_option<Index>().group_id;
		constexpr auto const &group = option_group<group_id>;
		constexpr auto multiple_size = []() -> std::size_t {
			if constexpr (is_valid_option_group_multiple_type_v<group_id>)
			{
				return option_group_multiple<group_id>.size();
			}
			else
			{
				return 0;
			}
		}();

		[&]<std::size_t ...GroupIs, std::size_t ...MultipleIs>(std::index_sequence<GroupIs...>, std::index_sequence<MultipleIs...>) {
			bool done = false;
			((([&]() {
				constexpr auto group_element_index = create_group_element_index(group_id, GroupIs);
				done = try_parse_group_element<group_element_index>(
					matched_option_name, arg_option_value, arg_arg_value,
					flag_position, error
				);
				return done;
			}()) || ...)
			|| (eq_it == arg.end() && (([&]() {
				done = try_parse_multiple_group_element<group_id, MultipleIs>(
					matched_option_name, arg_option_value, arg_arg_value,
					error
				);
				return done;
			}()) || ...)));
			if (!done)
			{
				error = fmt::format("invalid argument '{}' for option '{}'", arg_option_value, matched_option_name);
			}
		}(std::make_index_sequence<group.size() + (add_help_to_group<group_id> ? 1 : 0)>{}, std::make_index_sequence<multiple_size>{});

		return true;
	}
	else
	{
		return false;
	}
}

template<option_index_t Index, typename ArgFn>
bool try_parse_argument_flag(
	string_view option_value, std::size_t flag_position, bool is_single_char,
	string &error, ArgFn get_arg_fn
)
{
	static_assert(is_valid_options_type_v<get_options_id_t(Index)>, "The options array with the given ID isn't specialized or has an invalid type");
	constexpr auto usage = get_option<Index>().usage;
	constexpr auto option_names = get_flag_names(usage);
	constexpr auto option_names_with_dashes = get_flag_names_with_dashes(usage);
	constexpr bool has_first_option_name  = option_names.first  != "";
	constexpr bool has_second_option_name = option_names.second != "";

	auto const matched_arg_num =
		( is_single_char && has_first_option_name  && option_value == option_names.first)  ? 0 :
		(!is_single_char && has_second_option_name && option_value == option_names.second) ? 1 :
		-1;

	if (matched_arg_num != -1)
	{
		constexpr auto *flag = option_value_storage_ptr<Index>;
		auto const matched_option_name = matched_arg_num == 0
			? option_names_with_dashes.first
			: option_names_with_dashes.second;
		auto const arg_opt = get_arg_fn();
		if (!arg_opt.has_value())
		{
			error = fmt::format("expected an argument for option '{}'", matched_option_name);
			return true;
		}
		auto const arg = arg_opt.value();

		if (is_array_like<Index> || flag->flag_position == 0)
		{
			flag->flag_position = flag_position;
			flag->flag_value    = matched_option_name;
			flag->arg_value     = arg;
			if constexpr (has_argument_parse_function<Index>)
			{
				auto result = argument_parse_function<Index>(arg);
				if (result.has_value())
				{
					if constexpr (is_array_like<Index>)
					{
						flag->option_value.emplace_back(std::move(result.value()));
					}
					else
					{
						flag->option_value = std::move(result.value());
					}
				}
				else
				{
					error = fmt::format("invalid argument '{}' for option '{}'", arg, matched_option_name);
					return true;
				}
			}
			else
			{
				auto result = arg_parser<get_option<Index>().type>::parse(arg);
				if (result.has_value())
				{
					if constexpr (is_array_like<Index>)
					{
						flag->option_value.emplace_back(std::move(result.value()));
					}
					else
					{
						flag->option_value = std::move(result.value());
					}
				}
				else
				{
					error = fmt::format("invalid argument '{}' for option '{}'", arg, matched_option_name);
					return true;
				}
			}
		}
		else
		{
			error = fmt::format(
				"option '{}' has already been set by argument '{}' with the value '{}' at position {}",
				matched_option_name, flag->flag_value, flag->arg_value, flag->flag_position
			);
			return true;
		}
		return true;
	}
	else
	{
		return false;
	}
}

template<option_index_t Index, typename ArgFn>
bool try_parse_equals_flag(
	string_view option_value, std::size_t flag_position, bool is_single_char,
	string &error, ArgFn get_arg_fn
)
{
	static_assert(is_valid_options_type_v<get_options_id_t(Index)>, "The options array with the given ID isn't specialized or has an invalid type");
	constexpr auto usage = get_option<Index>().usage;
	constexpr auto option_names = get_flag_names(usage);
	constexpr auto option_names_with_dashes = get_flag_names_with_dashes(usage);
	constexpr bool has_first_option_name  = option_names.first  != "";
	constexpr bool has_second_option_name = option_names.second != "";

	auto const matched_arg_num =
		( is_single_char && has_first_option_name  && option_value == option_names.first)  ? 0 :
		(!is_single_char && has_second_option_name && option_value == option_names.second) ? 1 :
		-1;

	if (matched_arg_num != -1)
	{
		constexpr auto *flag = option_value_storage_ptr<Index>;
		auto const matched_option_name = matched_arg_num == 0
			? option_names_with_dashes.first
			: option_names_with_dashes.second;
		auto const arg_opt = get_arg_fn(true);
		if (!arg_opt.has_value())
		{
			error = fmt::format("expected an argument for option '{}'", matched_option_name);
			return true;
		}
		auto const arg = arg_opt.value();

		if (is_array_like<Index> || flag->flag_position == 0)
		{
			flag->flag_position = flag_position;
			flag->flag_value    = matched_option_name;
			flag->arg_value     = arg;
			if constexpr (has_argument_parse_function<Index>)
			{
				auto result = argument_parse_function<Index>(arg);
				if (result.has_value())
				{
					if constexpr (is_array_like<Index>)
					{
						flag->option_value.emplace_back(std::move(result.value()));
					}
					else
					{
						flag->option_value = std::move(result.value());
					}
				}
				else
				{
					error = fmt::format("invalid argument '{}' for option '{}'", arg, matched_option_name);
					return true;
				}
			}
			else
			{
				auto result = arg_parser<get_option<Index>().type>::parse(arg);
				if (result.has_value())
				{
					if constexpr (is_array_like<Index>)
					{
						flag->option_value.emplace_back(std::move(result.value()));
					}
					else
					{
						flag->option_value = std::move(result.value());
					}
				}
				else
				{
					error = fmt::format("invalid argument '{}' for option '{}'", arg, matched_option_name);
					return true;
				}
			}
		}
		else
		{
			error = fmt::format(
				"option '{}' has already been set by argument '{}' with the value '{}' at position {}",
				matched_option_name, flag->flag_value, flag->arg_value, flag->flag_position
			);
			return true;
		}
		return true;
	}
	else
	{
		return false;
	}
}

template<option_index_t Index, typename ArgFn>
bool try_parse_flag(
	string_view option_value, std::size_t flag_position, bool is_single_char,
	string &error, ArgFn get_arg_fn
)
{
	static_assert(is_valid_options_type_v<get_options_id_t(Index)>, "The options array with the given ID isn't specialized or has an invalid type");
	constexpr auto usage = get_option<Index>().usage;

	if constexpr (get_option<Index>().group_id != group_id_t::invalid)
	{
		return try_parse_group_flag<Index>(option_value, flag_position, is_single_char, error, get_arg_fn);
	}
	else if constexpr (is_bool_flag(usage))
	{
		return try_parse_bool_flag<Index>(option_value, flag_position, is_single_char, error);
	}
	else if constexpr (is_argument_flag(usage))
	{
		return try_parse_argument_flag<Index>(option_value, flag_position, is_single_char, error, get_arg_fn);
	}
	else
	{
		static_assert(is_equals_flag(usage));
		return try_parse_equals_flag<Index>(option_value, flag_position, is_single_char, error, get_arg_fn);
	}
}

template<options_id_t ID, std::size_t ...Is, typename ArgFn>
bool parse_options_helper(
	std::index_sequence<Is...>,
	string_view option_value, std::size_t flag_position, bool is_single_char,
	string &error, ArgFn get_arg_fn
)
{
	static_assert(is_valid_options_type_v<ID>, "The options array with the given ID isn't specialized or has an invalid type");
	return (try_parse_flag<create_option_index(ID, Is)>(
		option_value, flag_position, is_single_char,
		error, get_arg_fn
	) || ...);
}

} // namespace internal

struct error
{
	std::size_t flag_position;
	string      message;
};

namespace internal
{

template<options_id_t ID>
vector<error> parse_options_helper(iter_t stream, iter_t begin, iter_t end)
{
	static_assert(internal::is_valid_options_type_v<ID>, "The options array with the given ID isn't specialized or has an invalid type");
	constexpr auto options_count = command_line_options<ID>.size() + (add_help_option<ID> ? 1 : 0) + (add_verbose_option<ID> ? 1 : 0);
	vector<error> errors;
	if (stream == end)
	{
		return errors;
	}

	string error_string;
	while (stream != end)
	{
		auto const flag_position = static_cast<std::size_t>(stream - begin);
		string_view stream_value = *stream;
		if (stream_value == "--")
		{
			++stream;
			for (; stream != end; ++stream)
			{
				positional_arguments_storage<ID>.emplace_back(*stream);
			}
			break;
		}
		auto const get_arg_fn = [&stream_value, &stream, end](bool is_equals = false) -> optional<string_view> {
			if (stream_value.length() != 0)
			{
				if (is_equals)
				{
					if (stream_value.starts_with('='))
					{
						auto const result = stream_value.substr(1);
						stream_value = string_view{};
						return result;
					}
					else
					{
						return {};
					}
				}
				else
				{
					auto const result = stream_value;
					stream_value = string_view{};
					return result;
				}
			}
			else if (!is_equals)
			{
				++stream;
				if (stream != end)
				{
					return *stream;
				}
				else
				{
					return {};
				}
			}
			else
			{
				return {};
			}
		};

		if (stream_value.starts_with("--"))
		{
			if (auto const eq_it = std::find(stream_value.begin(), stream_value.end(), '='); eq_it != stream_value.end())
			{
				auto const option_value = string_view(stream_value.begin() + 2, eq_it);
				stream_value = string_view(eq_it, stream_value.end());
				if (!parse_options_helper<ID>(
					std::make_index_sequence<options_count>{},
					option_value, flag_position, false,
					error_string, get_arg_fn
				))
				{
					errors.emplace_back(error{ flag_position, fmt::format("unknown option '--{}'", option_value) });
				}
				stream_value = string_view{};
			}
			else
			{
				auto const option_value = stream_value.substr(2);
				stream_value = string_view{};
				if (!parse_options_helper<ID>(
					std::make_index_sequence<options_count>{},
					option_value, flag_position, false,
					error_string, get_arg_fn
				))
				{
					errors.emplace_back(error{ flag_position, fmt::format("unknown option '--{}'", option_value) });
				}
			}
			if (error_string != "")
			{
				errors.emplace_back(error{ flag_position, std::move(error_string) });
			}
			error_string.clear();
		}
		else if (stream_value.starts_with('-') && stream_value.length() > 1 /* not equal to "-" */)
		{
			stream_value = stream_value.substr(1);
			// if get_arg_fn is called, the stream should
			while (stream_value != "")
			{
				auto const option_value = stream_value.substr(0, 1);
				stream_value = stream_value.substr(1);
				if (!parse_options_helper<ID>(
					std::make_index_sequence<options_count>{},
					option_value, flag_position, true,
					error_string, get_arg_fn
				))
				{
					errors.emplace_back(error{ flag_position, fmt::format("unknown option '-{}'", option_value) });
				}

				if (error_string != "")
				{
					errors.emplace_back(error{ flag_position, std::move(error_string) });
				}
				error_string.clear();
			}
		}
		else
		{
			// positional arguments
			positional_arguments_storage<ID>.push_back(stream_value);
		}

		// get_arg_fn may change the value of stream, so it can be equal to `end` at this point
		if (stream != end)
		{
			++stream;
		}
	}
	return errors;
}

} // namespace internal

template<options_id_t ID = options_id_t::def>
[[nodiscard]] vector<error> parse_options(internal::iter_t stream, internal::iter_t begin, internal::iter_t end)
{
	static_assert(internal::is_valid_options_type_v<ID>, "The options array with the given ID isn't specialized or has an invalid type");
	return internal::parse_options_helper<ID>(stream, begin, end);
}

template<commands_id_t ID = commands_id_t::def>
[[nodiscard]] vector<error> parse_commands(internal::iter_t stream, internal::iter_t begin, internal::iter_t end)
{
	static_assert(internal::is_valid_commands_type_v<ID>, "The commands array with the given ID isn't specialized or has an invalid type");
	vector<error> errors;
	if (stream == end)
	{
		errors.push_back(error{ static_cast<std::size_t>(stream - begin), "expected a command" });
		return errors;
	}

	[&]<std::size_t ...Ns>(std::index_sequence<Ns...>) {
		bool done = false;
		auto const command_value = *stream;
		auto const command_position = static_cast<std::size_t>(stream - begin);

		if constexpr (add_help_command<ID>)
		{
			static_assert(internal::is_bool_flag(help_command<ID>.usage));
			constexpr auto help_names = internal::get_flag_names(help_command<ID>.usage);
			constexpr auto has_first_name  = help_names.first  != "";
			constexpr auto has_second_name = help_names.second != "";
			if (
				(has_first_name && help_names.first == command_value)
				|| (has_second_name && help_names.second == command_value)
			)
			{
				constexpr auto index = internal::create_command_index(ID, command_line_commands<ID>.size());
				constexpr auto *command_value_storage = internal::command_value_storage_ptr<index>;
				command_value_storage->option_value = true;
				command_value_storage->flag_position = command_position;
				command_value_storage->flag_value = command_value;
				return;
			}
		}

		++stream;
		(((void)(done || ([&]() {
			auto const &command = command_line_commands<ID>[Ns];
			constexpr auto options_id = command.options_id;
			constexpr auto names = internal::get_flag_names(command.usage);
			constexpr auto has_first_name  = names.first  != "";
			constexpr auto has_second_name = names.second != "";
			constexpr auto has_arg = internal::is_argument_flag(command.usage);
			if (
				(has_first_name && names.first == command_value)
				|| (has_second_name && names.second == command_value)
			)
			{
				done = true;
				constexpr auto index = internal::create_command_index(ID, Ns);
				constexpr auto *command_value_storage = internal::command_value_storage_ptr<index>;
				if constexpr (internal::get_command<index>().type == arg_type::none)
				{
					command_value_storage->option_value = true;
					command_value_storage->flag_position = command_position;
					command_value_storage->flag_value = command_value;
				}
				else if constexpr (internal::has_argument_parse_function<index>)
				{
					static_assert(has_arg);
					if (stream == end)
					{
						errors.push_back(error{ command_position, fmt::format("expected an argument for command '{}'", command_value) });
						return;
					}
					auto const arg = *stream;
					++stream;
					auto result = argument_parse_function<index>(arg);
					if (result.has_value())
					{
						command_value_storage->option_value = std::move(result.value());
						command_value_storage->flag_position = command_position;
						command_value_storage->flag_value = command_value;
						command_value_storage->arg_value = arg;
					}
					else
					{
						errors.push_back(error{ command_position, fmt::format("invalid argument '{}' for command '{}'", arg, command_value) });
						return;
					}
				}
				else
				{
					static_assert(has_arg);
					if (stream == end)
					{
						errors.push_back(error{ command_position, fmt::format("expected an argument for command '{}'", command_value) });
						return;
					}
					auto const arg = *stream;
					++stream;
					auto result = internal::arg_parser<internal::get_command<index>().type>::parse(arg);
					if (result.has_value())
					{
						command_value_storage->option_value = std::move(result.value());
						command_value_storage->flag_position = command_position;
						command_value_storage->flag_value = command_value;
						command_value_storage->arg_value = arg;
					}
					else
					{
						errors.push_back(error{ command_position, fmt::format("invalid argument '{}' for command '{}'", arg, command_value) });
						return;
					}
				}
				errors = internal::parse_options_helper<options_id>(
					stream, begin, end
				);
			}
		}(), false))), ...);
		if (!done)
		{
			errors.push_back(error{ command_position, fmt::format("unknown command '{}'", command_value) });
		}
	}(std::make_index_sequence<command_line_commands<ID>.size()>{});
	return errors;
}

template<auto ID = &internal::default_parse_tag>
[[nodiscard]] vector<error> parse_command_line(int argc, char const **argv)
{
	static_assert(
		ID == &internal::default_parse_tag
		|| std::is_same_v<decltype(ID), commands_id_t>
		|| std::is_same_v<decltype(ID), options_id_t>
	);
	assert(argc >= 1);
	auto const args = internal::create_args_vector(argc, argv);
	if constexpr (std::is_same_v<decltype(ID), commands_id_t>)
	{
		return parse_commands<ID>(args.begin() + 1, args.begin(), args.end());
	}
	else if constexpr (std::is_same_v<decltype(ID), options_id_t>)
	{
		return parse_options<ID>(args.begin() + 1, args.begin(), args.end());
	}
	else
	{
		constexpr auto default_commands_id = internal::default_commands_id<ID>;
		constexpr auto default_options_id = internal::default_options_id<ID>;
		if constexpr (internal::is_valid_commands_type_v<default_commands_id>)
		{
			return parse_commands<default_commands_id>(args.begin() + 1, args.begin(), args.end());
		}
		else
		{
			return parse_options<default_options_id>(args.begin() + 1, args.begin(), args.end());
		}
	}
}

bool alphabetical_compare(string_view lhs, string_view rhs);

inline bool compare_usages(string_view lhs, string_view rhs)
{
	auto const [lhs_first, lhs_second] = internal::get_flag_names(lhs);
	auto const [rhs_first, rhs_second] = internal::get_flag_names(rhs);
	auto const lhs_actual = lhs_first == "" ? lhs_second : lhs_first;
	auto const rhs_actual = rhs_first == "" ? rhs_second : rhs_first;
	return alphabetical_compare(lhs_actual, rhs_actual);
}

string get_help_string(
	vector<string> const &usages,
	vector<string> const &helps,
	std::size_t initial_indent_width,
	std::size_t usage_width,
	std::size_t column_limit
);

template<group_id_t ID>
string get_option_group_help_string(
	std::size_t initial_indent_width,
	std::size_t usage_width,
	std::size_t column_limit,
	bool is_verbose = false,
	bool sort_alphabetically = true
)
{
	constexpr auto group_elements_count = option_group<ID>.size() + (add_help_to_group<ID> ? 1 : 0);
	constexpr auto multiple_group_elements_count = []() -> std::size_t {
		if constexpr (internal::is_valid_option_group_multiple_type_v<ID>)
		{
			return option_group_multiple<ID>.size();
		}
		else
		{
			return 0;
		}
	}();

	vector<string> usages;
	vector<string> helps;
	usages.reserve(group_elements_count + (multiple_group_elements_count == 0 ? 0 : multiple_group_elements_count + 1));
	helps.reserve (group_elements_count + (multiple_group_elements_count == 0 ? 0 : multiple_group_elements_count + 1));

	if constexpr (multiple_group_elements_count != 0)
	{
		vector<std::size_t> multiple_indicies;
		multiple_indicies.resize(multiple_group_elements_count);
		for (std::size_t i = 0; i < multiple_indicies.size(); ++i)
		{
			multiple_indicies[i] = i;
		}

		if (sort_alphabetically)
		{
			std::sort(multiple_indicies.begin(), multiple_indicies.end(), [](std::size_t lhs, std::size_t rhs) {
				auto const lhs_usage = option_group_multiple<ID>[lhs].usage;
				auto const rhs_usage = option_group_multiple<ID>[rhs].usage;
				return compare_usages(lhs_usage, rhs_usage);
			});
		}

		for (auto const i : multiple_indicies)
		{
			auto const &element = option_group_multiple<ID>[i];
			if (
				element.visibility != visibility_kind::undocumented
				&& (is_verbose || element.visibility == visibility_kind::visible)
			)
			{
				usages.emplace_back(element.usage);
				helps.emplace_back(element.help);
			}
		}

		usages.emplace_back();
		helps.emplace_back();
	}

	vector<std::size_t> indicies;
	indicies.resize(group_elements_count - (add_help_to_group<ID> ? 1 : 0));
	for (std::size_t i = 0; i < indicies.size(); ++i)
	{
		indicies[i] = i;
	}

	if (sort_alphabetically)
	{
		std::sort(indicies.begin(), indicies.end(), [](std::size_t lhs, std::size_t rhs) {
			auto const lhs_usage = option_group<ID>[lhs].usage;
			auto const rhs_usage = option_group<ID>[rhs].usage;
			return compare_usages(lhs_usage, rhs_usage);
		});
	}

	if constexpr (add_help_to_group<ID>)
	{
		if (
			help_group_element<ID>.visibility != visibility_kind::undocumented
			&& (is_verbose || help_group_element<ID>.visibility == visibility_kind::visible)
		)
		{
			usages.emplace_back(help_group_element<ID>.usage);
			helps.emplace_back(help_group_element<ID>.help);
		}
	}

	for (auto const i : indicies)
	{
		auto const &group_element = option_group<ID>[i];
		if (
			group_element.visibility != visibility_kind::undocumented
			&& (is_verbose || group_element.visibility == visibility_kind::visible)
		)
		{
			usages.emplace_back(group_element.usage);
			helps.emplace_back(group_element.help);
		}
	}

	return get_help_string(usages, helps, initial_indent_width, usage_width, column_limit);
}

template<options_id_t ID>
string get_options_help_string(
	std::size_t initial_indent_width,
	std::size_t usage_width,
	std::size_t column_limit,
	bool is_verbose = false,
	bool sort_alphabetically = true
)
{
	constexpr auto options_count = command_line_options<ID>.size() + (add_help_option<ID> ? 1 : 0) + (add_verbose_option<ID> ? 1 : 0);
	vector<string> usages;
	vector<string> helps;
	usages.reserve(options_count);
	helps.reserve(options_count);

	vector<std::size_t> indicies;
	indicies.resize(options_count - (add_help_option<ID> ? 1 : 0));
	for (std::size_t i = 0; i < command_line_options<ID>.size(); ++i)
	{
		indicies[i] = i;
	}
	if constexpr (add_verbose_option<ID>)
	{
		indicies.back() = command_line_options<ID>.size() + (add_help_option<ID> ? 1 : 0);
	}

	if (sort_alphabetically)
	{
		std::sort(indicies.begin(), indicies.end(), [](std::size_t lhs, std::size_t rhs) {
			if constexpr (add_verbose_option<ID>)
			{
				auto const lhs_usage = lhs >= command_line_options<ID>.size() ? verbose_option<ID>.usage : command_line_options<ID>[lhs].usage;
				auto const rhs_usage = rhs >= command_line_options<ID>.size() ? verbose_option<ID>.usage : command_line_options<ID>[rhs].usage;
				return compare_usages(lhs_usage, rhs_usage);
			}
			else
			{
				auto const lhs_usage = command_line_options<ID>[lhs].usage;
				auto const rhs_usage = command_line_options<ID>[rhs].usage;
				return compare_usages(lhs_usage, rhs_usage);
			}
		});
	}

	if constexpr (add_help_option<ID>)
	{
		if (
			help_option<ID>.visibility != visibility_kind::undocumented
			&& (is_verbose || help_option<ID>.visibility == visibility_kind::visible)
		)
		{
			usages.emplace_back(
				help_option<ID>.usage.starts_with("--")
				? fmt::format("    {}", help_option<ID>.usage)
				: string(help_option<ID>.usage)
			);
			helps.emplace_back(help_option<ID>.help);
		}
	}
	for (auto const i : indicies)
	{
		if constexpr (add_verbose_option<ID>)
		{
			auto const &option = i >= command_line_options<ID>.size() ? verbose_option<ID> : command_line_options<ID>[i];
			if (
				option.visibility != visibility_kind::undocumented
				&& (is_verbose || option.visibility == visibility_kind::visible)
			)
			{
				usages.emplace_back(
					option.usage.starts_with("--")
					? fmt::format("    {}", option.usage)
					: string(option.usage)
				);
				helps.emplace_back(option.help);
			}
		}
		else
		{
			auto const &option = command_line_options<ID>[i];
			if (
				option.visibility != visibility_kind::undocumented
				&& (is_verbose || option.visibility == visibility_kind::visible)
			)
			{
				usages.emplace_back(
					option.usage.starts_with("--")
					? fmt::format("    {}", option.usage)
					: string(option.usage)
				);
				helps.emplace_back(option.help);
			}
		}
	}

	return get_help_string(usages, helps, initial_indent_width, usage_width, column_limit);
}

template<commands_id_t ID>
string get_commands_help_string(
	std::size_t initial_indent_width,
	std::size_t usage_width,
	std::size_t column_limit,
	bool is_verbose = false,
	bool sort_alphabetically = true
)
{
	constexpr auto commands_count = command_line_commands<ID>.size() + (add_help_command<ID> ? 1 : 0);
	vector<string> usages;
	vector<string> helps;
	usages.reserve(commands_count);
	helps.reserve(commands_count);

	vector<std::size_t> indicies;
	indicies.resize(commands_count - (add_help_command<ID> ? 1 : 0));
	for (std::size_t i = 0; i < command_line_commands<ID>.size(); ++i)
	{
		indicies[i] = i;
	}

	if (sort_alphabetically)
	{
		std::sort(indicies.begin(), indicies.end(), [](std::size_t lhs, std::size_t rhs) {
			auto const lhs_usage = command_line_commands<ID>[lhs].usage;
			auto const rhs_usage = command_line_commands<ID>[rhs].usage;
			return compare_usages(lhs_usage, rhs_usage);
		});
	}

	if constexpr (add_help_command<ID>)
	{
		if (
			help_command<ID>.visibility != visibility_kind::undocumented
			&& (is_verbose || help_command<ID>.visibility == visibility_kind::visible)
		)
		{
			usages.emplace_back(help_command<ID>.usage);
			helps.emplace_back(help_command<ID>.help);
		}
	}
	for (auto const i : indicies)
	{
		auto const &command = command_line_commands<ID>[i];
		if (
			command.visibility != visibility_kind::undocumented
			&& (is_verbose || command.visibility == visibility_kind::visible)
		)
		{
			usages.emplace_back(command.usage);
			helps.emplace_back(command.help);
		}
	}

	return get_help_string(usages, helps, initial_indent_width, usage_width, column_limit);
}


template<commands_id_t ID>
void print_commands_help(
	string_view executable_name,
	std::size_t initial_indent_width,
	std::size_t usage_width,
	std::size_t column_limit,
	bool sort_alphabetically = true
)
{
	auto const help_string = get_commands_help_string<ID>(
		initial_indent_width,
		usage_width,
		column_limit,
		sort_alphabetically
	);

	fmt::print(
		"Usage: {} <command> [options ...] {{positional-args ...}}\n\nCommands:\n{}",
		executable_name, help_string
	);
}

template<options_id_t ID>
void print_options_help(
	string_view executable_name,
	string_view positional_names,
	std::size_t initial_indent_width,
	std::size_t usage_width,
	std::size_t column_limit,
	bool sort_alphabetically = true
)
{
	if constexpr (add_verbose_option<ID>)
	{
		constexpr auto verbose_names_with_dashes = internal::get_flag_names_with_dashes(verbose_option<ID>.usage);
		auto const is_verbose = option_value<option<ID>(
			verbose_names_with_dashes.first != ""
			? verbose_names_with_dashes.first
			: verbose_names_with_dashes.second
		)>;
		auto const help_string = get_options_help_string<ID>(
			initial_indent_width,
			usage_width,
			column_limit,
			is_verbose,
			sort_alphabetically
		);
		auto const needs_additional_help_options_help = [&]() {
			bool result = false;
			for (auto const &option : command_line_options<ID>)
			{
				if (option.visibility == visibility_kind::hidden)
				{
					result = true;
				}
			}
			return result;
		}();

		if (is_verbose || !needs_additional_help_options_help)
		{
			fmt::print(
				"Usage: {} [options ...] {}\n\nOptions:\n{}",
				executable_name, positional_names, help_string
			);
		}
		else
		{
			auto const verbose_help_option = []() {
				constexpr auto help_flag_names    = internal::get_flag_names_with_dashes(help_option<ID>.usage);
				constexpr auto verbose_flag_names = internal::get_flag_names_with_dashes(verbose_option<ID>.usage);
				// prefer longer help option
				constexpr auto preferred_help_flag = help_flag_names.second != "" ? help_flag_names.second : help_flag_names.first;
				// prefer shorter verbose option
				constexpr auto preferred_verbose_flag = verbose_flag_names.first != "" ? verbose_flag_names.first : verbose_flag_names.second;
				return fmt::format("{} {}", preferred_help_flag, preferred_verbose_flag);
			}();
			auto const additional_help_string = get_help_string(
				{ verbose_help_option }, { "Display all available options" },
				initial_indent_width, usage_width, column_limit
			);
			fmt::print(
				"Usage: {} [options ...] {}\n\nOptions:\n{}\nAdditional help:\n{}",
				executable_name, positional_names,
				help_string, additional_help_string
			);
		}
	}
	else
	{
		auto const help_string = get_options_help_string<ID>(
			initial_indent_width,
			usage_width,
			column_limit,
			false,
			sort_alphabetically
		);
		fmt::print(
			"Usage: {} [options ...] {}\n\nOptions:\n{}",
			executable_name, positional_names, help_string
		);
	}
}

template<internal::command_index_t Index>
void print_command_options_help(
	string_view executable_name,
	std::size_t initial_indent_width,
	std::size_t usage_width,
	std::size_t column_limit,
	bool sort_alphabetically = true
)
{
	constexpr auto const &comm = internal::get_command<Index>();
	constexpr auto ID = comm.options_id;

	if constexpr (add_verbose_option<ID>)
	{
		auto const is_verbose = option_value<internal::verbose_option_index<ID>>;
		auto const help_string = get_options_help_string<ID>(
			initial_indent_width,
			usage_width,
			column_limit,
			is_verbose,
			sort_alphabetically
		);
		auto const needs_additional_help_options_help = [&]() {
			bool result = false;
			for (auto const &option : command_line_options<ID>)
			{
				if (option.visibility == visibility_kind::hidden)
				{
					result = true;
				}
			}
			return result;
		}();

		if (is_verbose || !needs_additional_help_options_help)
		{
			fmt::print(
				"Usage: {} {} [options ...] {}\n\nOptions:\n{}",
				executable_name, comm.usage, comm.positional_names, help_string
			);
		}
		else
		{
			auto const verbose_help_option = []() {
				constexpr auto help_flag_names    = internal::get_flag_names_with_dashes(help_option<ID>.usage);
				constexpr auto verbose_flag_names = internal::get_flag_names_with_dashes(verbose_option<ID>.usage);
				// prefer longer help option
				constexpr auto preferred_help_flag = help_flag_names.second != "" ? help_flag_names.second : help_flag_names.first;
				// prefer shorter verbose option
				constexpr auto preferred_verbose_flag = verbose_flag_names.first != "" ? verbose_flag_names.first : verbose_flag_names.second;
				return fmt::format("{} {}", preferred_help_flag, preferred_verbose_flag);
			}();
			auto const additional_help_string = get_help_string(
				{ verbose_help_option }, { "Display all available options" },
				initial_indent_width, usage_width, column_limit
			);
			fmt::print(
				"Usage: {} {} [options ...] {}\n\nOptions:\n{}\nAdditional help:\n{}",
				executable_name, comm.usage, comm.positional_names,
				help_string, additional_help_string
			);
		}
	}
	else
	{
		auto const help_string = get_options_help_string<ID>(
			initial_indent_width,
			usage_width,
			column_limit,
			false,
			sort_alphabetically
		);
		fmt::print(
			"Usage: {} {} [options ...] {}\n\nOptions:\n{}",
			executable_name, comm.usage, comm.positional_names, help_string
		);
	}
}

template<internal::option_index_t Index>
void print_option_group_help(
	std::size_t initial_indent_width,
	std::size_t usage_width,
	std::size_t column_limit,
	bool sort_alphabetically = true
)
{
	constexpr auto const &opt = internal::get_option<Index>();
	constexpr auto group_id = opt.group_id;
	static_assert(group_id != group_id_t::invalid);
	static_assert(add_help_to_group<group_id>);

	auto const is_verbose = []() {
		constexpr auto options_id = internal::get_options_id_t(Index);
		if constexpr (add_verbose_option<options_id>)
		{
			return option_value<internal::verbose_option_index<options_id>>;
		}
		else
		{
			return false;
		}
	}();
	auto const help_string = get_option_group_help_string<group_id>(
		initial_indent_width,
		usage_width,
		column_limit,
		is_verbose,
		sort_alphabetically
	);
	fmt::print("Available {}:\n{}", opt.group_name, help_string);
}


namespace internal
{

template<commands_id_t ID>
bool print_commands_help_if_needed(
	string_view executable_name,
	std::size_t initial_indent_width,
	std::size_t usage_width,
	std::size_t column_limit,
	bool sort_alphabetically
)
{
	if constexpr (add_help_command<ID>)
	{
		if (command_value<help_command_index<ID>>)
		{
			print_commands_help<ID>(
				executable_name,
				initial_indent_width,
				usage_width,
				column_limit,
				sort_alphabetically
			);
			return true;
		}
	}
	return false;
}

template<command_index_t Index>
bool print_command_options_help_if_needed(
	string_view executable_name,
	std::size_t initial_indent_width,
	std::size_t usage_width,
	std::size_t column_limit,
	bool sort_alphabetically
)
{
	constexpr auto ID = get_command<Index>().options_id;
	if constexpr (add_help_option<ID>)
	{
		if (option_value<help_option_index<ID>>)
		{
			print_command_options_help<Index>(
				executable_name,
				initial_indent_width,
				usage_width,
				column_limit,
				sort_alphabetically
			);
			return true;
		}
	}
	return false;
}

template<options_id_t ID>
bool print_options_help_if_needed(
	string_view executable_name,
	string_view positional_names,
	std::size_t initial_indent_width,
	std::size_t usage_width,
	std::size_t column_limit,
	bool sort_alphabetically
)
{
	if constexpr (add_help_option<ID>)
	{
		if (option_value<help_option_index<ID>>)
		{
			print_options_help<ID>(
				executable_name,
				positional_names,
				initial_indent_width,
				usage_width,
				column_limit,
				sort_alphabetically
			);
			return true;
		}
	}
	return false;
}

template<internal::option_index_t Index>
bool print_option_group_help_if_needed(
	std::size_t initial_indent_width,
	std::size_t usage_width,
	std::size_t column_limit,
	bool sort_alphabetically
)
{
	constexpr auto ID = get_option<Index>().group_id;
	if constexpr (ID != group_id_t::invalid && add_help_to_group<ID>)
	{
		if (group_element_value<internal::help_group_element_index<ID>>)
		{
			print_option_group_help<Index>(
				initial_indent_width,
				usage_width,
				column_limit,
				sort_alphabetically
			);
			return true;
		}
	}
	return false;
}

} // namespace internal

template<auto ID = &internal::default_parse_tag>
bool print_help_if_needed(
	string_view executable_name,
	std::size_t initial_indent_width,
	std::size_t usage_width,
	std::size_t column_limit,
	bool sort_alphabetically = true
)
{
	constexpr auto commands_id_opt = []() -> optional<commands_id_t> {
		if constexpr (std::is_same_v<decltype(ID), commands_id_t>)
		{
			static_assert(internal::is_valid_commands_type_v<ID>);
			return ID;
		}
		else if constexpr (internal::is_valid_commands_type_v<internal::default_commands_id<ID>>)
		{
			return internal::default_commands_id<ID>;
		}
		else
		{
			return {};
		}
	}();

	if constexpr (commands_id_opt.has_value())
	{
		constexpr auto commands_id = commands_id_opt.value();
		if (internal::print_commands_help_if_needed<commands_id>(
			executable_name,
			initial_indent_width,
			usage_width,
			column_limit,
			sort_alphabetically
		))
		{
			return true;
		}

		if ([&]<std::size_t ...Is>(std::index_sequence<Is...>) {
			return (([&]() {
				constexpr auto index = internal::create_command_index(commands_id, Is);
				if (!is_command_set<index>())
				{
					return false;
				}

				if (internal::print_command_options_help_if_needed<index>(
					executable_name,
					initial_indent_width,
					usage_width,
					column_limit,
					sort_alphabetically
				))
				{
					return true;
				}

				constexpr auto options_id = internal::get_command<index>().options_id;

				if ([&]<std::size_t ...Js>(std::index_sequence<Js...>) {
					return ((internal::print_option_group_help_if_needed<internal::create_option_index(options_id, Js)>(
						initial_indent_width,
						usage_width,
						column_limit,
						sort_alphabetically
					)) || ...);
				}(std::make_index_sequence<command_line_options<options_id>.size()>{}))
				{
					return true;
				}

				return false;
			}()) || ...);
		}(std::make_index_sequence<command_line_commands<commands_id>.size()>{}))
		{
			return true;
		}
	}

	constexpr auto options_id_opt = []() -> optional<options_id_t> {
		if constexpr (std::is_same_v<decltype(ID), options_id_t>)
		{
			static_assert(internal::is_valid_options_type_v<ID>);
			return ID;
		}
		else if constexpr (internal::is_valid_options_type_v<internal::default_options_id<ID>>)
		{
			return internal::default_options_id<ID>;
		}
		else
		{
			return {};
		}
	}();

	if constexpr (options_id_opt.has_value())
	{
		constexpr auto options_id = options_id_opt.value();
		if (internal::print_options_help_if_needed<options_id>(
			executable_name,
			"",
			initial_indent_width,
			usage_width,
			column_limit,
			sort_alphabetically
		))
		{
			return true;
		}

		if ([&]<std::size_t ...Is>(std::index_sequence<Is...>) {
			return (internal::print_option_group_help_if_needed<internal::create_option_index(options_id, Is)>(
				initial_indent_width,
				usage_width,
				column_limit,
				sort_alphabetically
			) || ...);
		}(std::make_index_sequence<command_line_options<options_id>.size()>{}))
		{
			return true;
		}
	}

	return false;
}

} // namespace ctcli

#undef CTCLI_UNREACHABLE
#undef CTCLI_SYNTAX_ASSERT

#endif //CTCLI_CTCLI_H
