#ifndef STENOS_TESTING_HPP
#define STENOS_TESTING_HPP

#include <stenos/bits.hpp>


template<class T>
struct CountAlloc
{
	typedef T value_type;
	typedef T* pointer;
	typedef const T* const_pointer;
	typedef T& reference;
	typedef const T& const_reference;
	using size_type = size_t;
	using difference_type = ptrdiff_t;
	using is_always_equal = std::false_type;
	using propagate_on_container_swap = std::true_type;
	using propagate_on_container_copy_assignment = std::true_type;
	using propagate_on_container_move_assignment = std::true_type;

	template<class Other>
	struct rebind
	{
		using other = CountAlloc<Other>;
	};

	std::shared_ptr<std::int64_t> d_count;

	CountAlloc()
	  : d_count(new std::int64_t(0))
	{
	}
	CountAlloc(const CountAlloc& other)
	  : d_count(other.d_count)
	{
	}
	template<class Other>
	CountAlloc(const CountAlloc<Other>& other)
	  : d_count(other.d_count)
	{
	}
	~CountAlloc() {}
	CountAlloc& operator=(const CountAlloc& other)
	{
		d_count = other.d_count;
		return *this;
	}

	bool operator==(const CountAlloc& other) const { return d_count == other.d_count; }
	bool operator!=(const CountAlloc& other) const { return d_count != other.d_count; }

	void deallocate(T* p, const size_t count)
	{
		std::allocator<T>{}.deallocate(p, count);
		(*d_count) -= count * sizeof(T);
	}
	T* allocate(const size_t count)
	{
		T* p = std::allocator<T>{}.allocate(count);
		(*d_count) += count * sizeof(T);
		return p;
	}
	T* allocate(const size_t count, const void*) { return allocate(count); }
	size_t max_size() const noexcept { return static_cast<size_t>(-1) / sizeof(T); }
};

template<class T>
std::int64_t get_alloc_bytes(const CountAlloc<T>& al)
{
	return *al.d_count;
}
template<class T>
std::int64_t get_alloc_bytes(const std::allocator<T>&)
{
	return 0;
}

#endif