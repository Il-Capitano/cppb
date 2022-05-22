#ifndef RANGES_RANGES_H
#define RANGES_RANGES_H

#include <type_traits>
#include <vector>
#include <algorithm>
#include <utility>

namespace ranges
{

template<typename Range>
struct universal_end_sentinel {};


namespace detail
{

template<typename Range>
struct range_base_filter
{
	template<typename Func>
	auto filter(Func &&func) const noexcept;
};

template<typename Collection>
struct collection_base_filter
{
	template<typename Func>
	auto filter(Func &&func) const noexcept;
};

template<typename Range>
struct range_base_transform
{
	template<typename Func>
	auto transform(Func &&func) const noexcept;
};

template<typename Collection>
struct collection_base_transform
{
	template<typename Func>
	auto transform(Func &&func) const noexcept;
};

template<typename Range>
struct range_base_collect
{
	template<template<typename T> typename Vec>
	auto collect(void) const noexcept;

	auto collect(void) const noexcept;
};

template<typename Range>
struct range_base_is_any
{
	template<typename Func>
	bool is_any(Func &&func) const noexcept;
};

template<typename Collection>
struct collection_base_is_any
{
	template<typename Func>
	bool is_any(Func &&func) const noexcept;
};

template<typename Range>
struct range_base_is_all
{
	template<typename Func>
	bool is_all(Func &&func) const noexcept;
};

template<typename Collection>
struct collection_base_is_all
{
	template<typename Func>
	bool is_all(Func &&func) const noexcept;
};

template<typename Range>
struct range_base_for_each
{
	template<typename Func>
	void for_each(Func &&func) const noexcept;
};

template<typename Collection>
struct collection_base_for_each
{
	template<typename Func>
	void for_each(Func &&func) const noexcept;
};

template<typename Range>
struct range_base_sum
{
	auto sum(void) const noexcept;
};

template<typename Collection>
struct collection_base_sum
{
	auto sum(void) const noexcept;
};

template<typename Range>
struct range_base_max
{
	auto max(void) const noexcept;
	template<typename T>
	auto max(T &&initial_value) const noexcept;
};

template<typename Collection>
struct collection_base_max
{
	auto max(void) const noexcept;
	template<typename T>
	auto max(T &&initial_value) const noexcept;
};

template<typename Collection>
struct collection_base_sort
{
	void sort(void) noexcept;
	template<typename Cmp>
	void sort(Cmp &&cmp) noexcept;
};

} // namespace detail


template<typename Range>
struct range_base :
	detail::range_base_filter   <Range>,
	detail::range_base_transform<Range>,
	detail::range_base_collect  <Range>,
	detail::range_base_is_any   <Range>,
	detail::range_base_is_all   <Range>,
	detail::range_base_for_each <Range>,
	detail::range_base_sum      <Range>,
	detail::range_base_max      <Range>
{};

template<typename Collection>
struct collection_base :
	detail::collection_base_filter   <Collection>,
	detail::collection_base_transform<Collection>,
	detail::collection_base_is_any   <Collection>,
	detail::collection_base_is_all   <Collection>,
	detail::collection_base_for_each <Collection>,
	detail::collection_base_sum      <Collection>,
	detail::collection_base_max      <Collection>,
	detail::collection_base_sort     <Collection>
{
	auto as_range(void) const noexcept;
};


template<typename ItType, typename EndType>
struct basic_range : range_base<basic_range<ItType, EndType>>
{
private:
	using self_t = basic_range<ItType, EndType>;
private:
	ItType _it;
	[[no_unique_address]] EndType _end;

public:
	basic_range(ItType it, EndType end)
		: _it(std::move(it)), _end(std::move(end))
	{}

	template<typename T, std::size_t N>
	basic_range(T (&arr)[N]) noexcept
		: _it(std::begin(arr)), _end(std::end(arr))
	{}

	template<typename T, std::size_t N>
	basic_range(T const (&arr)[N]) noexcept
		: _it(std::begin(arr)), _end(std::end(arr))
	{}

	template<typename Range>
	basic_range(Range const &range) noexcept
		: _it(range.begin()), _end(range.end())
	{}

	bool at_end(void) const noexcept
	{ return this->_it == this->_end; }

	self_t &operator ++ (void)
	{
		++this->_it;
		return *this;
	}

	decltype(auto) operator * (void) const noexcept
	{ return *this->_it; }

	auto const &operator -> (void) const noexcept
	{ return this->_it; }

	friend bool operator == (self_t const &lhs, [[maybe_unused]] universal_end_sentinel<self_t> rhs) noexcept
	{ return lhs.at_end(); }

	friend bool operator == ([[maybe_unused]] universal_end_sentinel<self_t> lhs, self_t const &rhs) noexcept
	{ return rhs.at_end(); }

	friend bool operator != (self_t const &lhs, [[maybe_unused]] universal_end_sentinel<self_t> rhs) noexcept
	{ return !lhs.at_end(); }

	friend bool operator != ([[maybe_unused]] universal_end_sentinel<self_t> lhs, self_t const &rhs) noexcept
	{ return !rhs.at_end(); }


	self_t begin(void) const noexcept
	{ return *this; }

	universal_end_sentinel<self_t> end(void) const noexcept
	{ return universal_end_sentinel<self_t>{}; }
};

template<typename T>
struct iota_range : range_base<iota_range<T>>
{
private:
	using self_t = iota_range<T>;
private:
	T _it;
	T _end;

public:
	iota_range(T it, T end)
		: _it(std::move(it)), _end(std::move(end))
	{}

	bool at_end(void) const noexcept
	{ return this->_it == this->_end; }

	self_t &operator ++ (void)
	{
		++this->_it;
		return *this;
	}

	decltype(auto) operator * (void) const noexcept
	{ return this->_it; }

	auto const &operator -> (void) const noexcept
	{ return &this->_it; }

	friend bool operator == (self_t const &lhs, [[maybe_unused]] universal_end_sentinel<self_t> rhs) noexcept
	{ return lhs.at_end(); }

	friend bool operator == ([[maybe_unused]] universal_end_sentinel<self_t> lhs, self_t const &rhs) noexcept
	{ return rhs.at_end(); }

	friend bool operator != (self_t const &lhs, [[maybe_unused]] universal_end_sentinel<self_t> rhs) noexcept
	{ return !lhs.at_end(); }

	friend bool operator != ([[maybe_unused]] universal_end_sentinel<self_t> lhs, self_t const &rhs) noexcept
	{ return !rhs.at_end(); }


	self_t begin(void) const noexcept
	{ return *this; }

	universal_end_sentinel<self_t> end(void) const noexcept
	{ return universal_end_sentinel<self_t>{}; }
};

template<typename ItType, typename EndType, typename FilterFuncType>
struct filter_range : range_base<filter_range<ItType, EndType, FilterFuncType>>
{
private:
	using self_t = filter_range<ItType, EndType, FilterFuncType>;
private:
	ItType _it;
	[[no_unique_address]] EndType _end;
	[[no_unique_address]] FilterFuncType _filter_function;

public:
	filter_range(ItType it, EndType end, FilterFuncType func)
		: _it(std::move(it)), _end(std::move(end)), _filter_function(std::move(func))
	{
		while (this->_it != this->_end && !this->_filter_function(*this->_it))
		{
			++this->_it;
		}
	}

	bool at_end(void) const noexcept
	{ return this->_it == this->_end; }

	self_t &operator ++ (void)
	{
		do
		{
			++this->_it;
		} while (this->_it != this->_end && !this->_filter_function(*this->_it));
		return *this;
	}

	decltype(auto) operator * (void) const noexcept
	{ return *this->_it; }

	auto const &operator -> (void) const noexcept
	{ return this->_it; }

	friend bool operator == (self_t const &lhs, [[maybe_unused]] universal_end_sentinel<self_t> rhs) noexcept
	{ return lhs.at_end(); }

	friend bool operator == ([[maybe_unused]] universal_end_sentinel<self_t> lhs, self_t const &rhs) noexcept
	{ return rhs.at_end(); }

	friend bool operator != (self_t const &lhs, [[maybe_unused]] universal_end_sentinel<self_t> rhs) noexcept
	{ return !lhs.at_end(); }

	friend bool operator != ([[maybe_unused]] universal_end_sentinel<self_t> lhs, self_t const &rhs) noexcept
	{ return !rhs.at_end(); }


	self_t begin(void) const noexcept
	{ return *this; }

	universal_end_sentinel<self_t> end(void) const noexcept
	{ return universal_end_sentinel<self_t>{}; }
};

template<typename ItType, typename EndType, typename TransformFuncType>
struct transform_range : range_base<transform_range<ItType, EndType, TransformFuncType>>
{
private:
	using self_t = transform_range<ItType, EndType, TransformFuncType>;
private:
	ItType _it;
	[[no_unique_address]] EndType _end;
	[[no_unique_address]] TransformFuncType _transform_func;

public:
	transform_range(ItType it, EndType end, TransformFuncType func)
		: _it(std::move(it)), _end(std::move(end)), _transform_func(std::move(func))
	{}

	bool at_end(void) const noexcept
	{ return this->_it == this->_end; }

	self_t &operator ++ (void)
	{
		++this->_it;
		return *this;
	}

	decltype(auto) operator * (void) const noexcept
	{ return this->_transform_func(*this->_it); }

	auto const &operator -> (void) const noexcept
	{ return this->_it; }

	friend bool operator == (self_t const &lhs, [[maybe_unused]] universal_end_sentinel<self_t> rhs) noexcept
	{ return lhs.at_end(); }

	friend bool operator == ([[maybe_unused]] universal_end_sentinel<self_t> lhs, self_t const &rhs) noexcept
	{ return rhs.at_end(); }

	friend bool operator != (self_t const &lhs, [[maybe_unused]] universal_end_sentinel<self_t> rhs) noexcept
	{ return !lhs.at_end(); }

	friend bool operator != ([[maybe_unused]] universal_end_sentinel<self_t> lhs, self_t const &rhs) noexcept
	{ return !rhs.at_end(); }


	self_t begin(void) const noexcept
	{ return *this; }

	universal_end_sentinel<self_t> end(void) const noexcept
	{ return universal_end_sentinel<self_t>{}; }
};


template<typename ItType, typename EndType>
basic_range(ItType it, EndType end) -> basic_range<ItType, EndType>;

template<typename T, std::size_t N>
basic_range(T (&arr)[N]) -> basic_range<decltype(std::begin(arr)), decltype(std::end(arr))>;

template<typename T, std::size_t N>
basic_range(T const (&arr)[N]) -> basic_range<decltype(std::begin(arr)), decltype(std::end(arr))>;

template<typename Range>
basic_range(Range const &range) -> basic_range<decltype(range.begin()), decltype(range.end())>;

template<typename T>
iota_range(T it, T end) -> iota_range<T>;

template<typename ItType, typename EndType, typename FilterFuncType>
filter_range(ItType it, EndType end, FilterFuncType func) -> filter_range<ItType, EndType, FilterFuncType>;

template<typename ItType, typename EndType, typename TransformFuncType>
transform_range(ItType it, EndType end, TransformFuncType func) -> transform_range<ItType, EndType, TransformFuncType>;


template<typename Collection>
auto collection_base<Collection>::as_range(void) const noexcept
{
	auto const self = static_cast<Collection const *>(this);
	return basic_range{ self->begin(), self->end() };
}

template<
	typename Range,
	std::enable_if_t<std::is_reference_v<Range> && !std::is_rvalue_reference_v<Range>, int> = 0
>
auto to_range(Range &&range) noexcept
{
	return basic_range{
		std::forward<Range>(range).begin(),
		std::forward<Range>(range).end()
	};
}

template<typename T>
auto iota(T begin, T end) noexcept
{
	return iota_range{
		std::move(begin),
		std::move(end)
	};
}

template<
	typename Range, typename Func,
	std::enable_if_t<std::is_reference_v<Range> && !std::is_rvalue_reference_v<Range>, int> = 0
>
auto filter(Range &&range, Func &&func) noexcept
{
	return filter_range{
		std::forward<Range>(range).begin(),
		std::forward<Range>(range).end(),
		std::forward<Func>(func)
	};
}

template<
	typename Range, typename Func,
	std::enable_if_t<std::is_reference_v<Range> && !std::is_rvalue_reference_v<Range>, int> = 0
>
auto transform(Range &&range, Func &&func) noexcept
{
	return transform_range{
		std::forward<Range>(range).begin(),
		std::forward<Range>(range).end(),
		std::forward<Func>(func)
	};
}


namespace detail
{

template<typename Range>
template<typename Func>
auto range_base_filter<Range>::filter(Func &&func) const noexcept
{ return ::ranges::filter(*static_cast<Range const *>(this), std::forward<Func>(func)); }

template<typename Collection>
template<typename Func>
auto collection_base_filter<Collection>::filter(Func &&func) const noexcept
{ return static_cast<Collection const *>(this)->as_range().filter(std::forward<Func>(func)); }

template<typename Range>
template<typename Func>
auto range_base_transform<Range>::transform(Func &&func) const noexcept
{ return ::ranges::transform(*static_cast<Range const *>(this), std::forward<Func>(func)); }

template<typename Collection>
template<typename Func>
auto collection_base_transform<Collection>::transform(Func &&func) const noexcept
{ return static_cast<Collection const *>(this)->as_range().transform(std::forward<Func>(func)); }

template<typename Range>
template<template<typename T> typename Vec>
auto range_base_collect<Range>::collect(void) const noexcept
{
	auto const self = static_cast<Range const *>(this);
	Vec<std::decay_t<decltype(self->operator*())>> result;
	for (auto &&it : *self)
	{
		result.emplace_back(std::forward<decltype(it)>(it));
	}
	return result;
}

template<typename Range>
template<typename Func>
bool range_base_is_any<Range>::is_any(Func &&func) const noexcept
{
	auto const self = static_cast<Range const *>(this);
	for (auto &&it : *self)
	{
		if (func(std::forward<decltype(it)>(it)))
		{
			return true;
		}
	}
	return false;
}

template<typename Collection>
template<typename Func>
bool collection_base_is_any<Collection>::is_any(Func &&func) const noexcept
{ return static_cast<Collection const *>(this)->as_range().is_any(std::forward<Func>(func)); }

template<typename Range>
template<typename Func>
bool range_base_is_all<Range>::is_all(Func &&func) const noexcept
{
	auto const self = static_cast<Range const *>(this);
	for (auto &&it : *self)
	{
		if (!func(std::forward<decltype(it)>(it)))
		{
			return false;
		}
	}
	return true;
}

template<typename Collection>
template<typename Func>
bool collection_base_is_all<Collection>::is_all(Func &&func) const noexcept
{ return static_cast<Collection const *>(this)->as_range().is_all(std::forward<Func>(func)); }

template<typename Range>
template<typename Func>
void range_base_for_each<Range>::for_each(Func &&func) const noexcept
{
	auto const self = static_cast<Range const *>(this);
	for (auto &&it : *self)
	{
		func(std::forward<decltype(it)>(it));
	}
}

template<typename Collection>
template<typename Func>
void collection_base_for_each<Collection>::for_each(Func &&func) const noexcept
{ return static_cast<Collection const *>(this)->as_range().for_each(std::forward<Func>(func)); }

template<typename Range>
auto range_base_sum<Range>::sum(void) const noexcept
{
	auto const self = static_cast<Range const *>(this);
	std::decay_t<decltype(self->operator*())> result{};
	for (auto &&it : *self)
	{
		result += std::forward<decltype(it)>(it);
	}
	return result;
}

template<typename Collection>
auto collection_base_sum<Collection>::sum(void) const noexcept
{ return static_cast<Collection const *>(this)->as_range().sum(); }

template<typename Range>
auto range_base_max<Range>::max(void) const noexcept
{
	auto const self = static_cast<Range const *>(this);
	std::decay_t<decltype(self->operator*())> result{};
	for (auto &&it : *self)
	{
		if (it > result)
		{
			result = std::forward<decltype(it)>(it);
		}
	}
	return result;
}

template<typename Range>
template<typename T>
auto range_base_max<Range>::max(T &&initial_value) const noexcept
{
	auto const self = static_cast<Range const *>(this);
	std::decay_t<decltype(self->operator*())> result{ std::forward<T>(initial_value) };
	for (auto &&it : *self)
	{
		if (it > result)
		{
			result = std::forward<decltype(it)>(it);
		}
	}
	return result;
}

template<typename Collection>
auto collection_base_max<Collection>::max(void) const noexcept
{ return static_cast<Collection const *>(this)->as_range().sum(); }

template<typename Collection>
template<typename T>
auto collection_base_max<Collection>::max(T &&initial_value) const noexcept
{ return static_cast<Collection const *>(this)->as_range().sum(std::forward<T>(initial_value)); }

template<typename Collection>
void collection_base_sort<Collection>::sort(void) noexcept
{
	auto const self = static_cast<Collection const *>(this);
	std::sort(self->begin(), self->end());
}

template<typename Collection>
template<typename Cmp>
void collection_base_sort<Collection>::sort(Cmp &&cmp) noexcept
{
	auto const self = static_cast<Collection *>(this);
	std::sort(self->begin(), self->end(), std::forward<Cmp>(cmp));
}

} // namespace detail

} // namespace ranges

#endif // RANGES_RANGES_H
