#ifndef CORE_H
#define CORE_H

#include <vector>
#include <array>
#include <filesystem>
#include <string_view>
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
struct array_view : ranges::basic_range<T *, T *>
{
	array_view()
		: ranges::basic_range<T *, T *>(nullptr, nullptr)
	{}

	template<typename Range>
	array_view(Range &&range)
		: ranges::basic_range<T *, T *>(&*range.begin(), &*range.end())
	{}
};

} // namespace cppb

#endif // CORE_H
