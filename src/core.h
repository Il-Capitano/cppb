#ifndef CORE_H
#define CORE_H

#include <vector>
#include <array>
#include <filesystem>
#include <string_view>
#include <type_traits>
#include <span>
#include <fmt/format.h>

#include "ranges/ranges.h"

namespace fs = std::filesystem;

namespace cppb
{

template<typename T>
struct vector : std::vector<T>, ranges::collection_base<vector<T>>
{
	template<typename Range>
	void append(Range &&range)
	{
		for (auto &&val : std::forward<Range>(range))
		{
			this->emplace_back(std::forward<decltype(val)>(val));
		}
	}
};

template<typename T, std::size_t N>
struct array : std::array<T, N>, ranges::collection_base<array<T, N>>
{};

template<typename T>
struct span : std::span<T>, ranges::collection_base<span<T>>
{
	using std::span<T>::span;
};

} // namespace cppb

#endif // CORE_H
