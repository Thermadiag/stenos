/**
 * MIT License
 *
 * Copyright (c) 2025 Victor Moncada <vtr.moncada@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef STENOS_VECTOR_HPP
#define STENOS_VECTOR_HPP

#include <type_traits>
#include <memory>

namespace stenos
{
	/// @brief Type trait telling if a class is relocatable or not.
	///
	/// A type is considered relocatable if these consecutive calls
	/// \code{.cpp}
	/// new(new_place) T(std::move(old_place));
	/// old_place.~T();
	/// \endcode
	/// can be replaced by
	/// \code{.cpp}
	/// memcpy(&new_place, &old_place, sizeof(T));
	/// \endcode
	///
	///
	template<class T>
	struct is_relocatable
	{
		static constexpr bool value = std::is_trivially_copyable<T>::value && std::is_trivially_destructible<T>::value;
	};

	template<class T, class D>
	struct is_relocatable<std::unique_ptr<T, D>> : std::true_type
	{
	};

	namespace detail
	{
		// forward declaration
		template<class Compress>
		class RefWrapper;
	}
}
namespace std
{
	///////////////////////////
	// Completely illegal overload of std::move.
	// That's currently the only way I found to use generic algorithms (like std::move(It, It, Dst) ) with cvector.
	///////////////////////////

	/* template<class Compress>
	typename Compress::value_type move(stenos::detail::RefWrapper<Compress>& other) noexcept;
	*/
	// template<class Compress>
	// typename Compress::value_type move(stenos::detail::RefWrapper<Compress>&& other) noexcept;

}

#include <algorithm>
#include <vector>
#include <atomic>
#include <limits>
#include <thread>
#include <mutex>
#include <iterator>
#include <shared_mutex>

#include "stenos.h"
#include "bits.hpp"

namespace stenos
{
	namespace detail
	{
		// Returns distance between 2 iterators, or 0 for non random access iterators
		template<class Iter, class Cat>
		auto iter_distance(const Iter&, const Iter&, Cat /*unused*/) noexcept -> size_t
		{
			return 0;
		}
		template<class Iter>
		auto iter_distance(const Iter& first, const Iter& last, std::random_access_iterator_tag /*unused*/) noexcept -> size_t
		{
			return (last > first) ? static_cast<size_t>(last - first) : 0;
		}

		// Equivalent to void_t
		template<class T>
		struct make_void
		{
			using type = void;
		};

		// Check if an allcoator has is_always_equal type
		template<class T, class = void>
		struct has_is_always_equal : std::false_type
		{
		};

		template<class T>
		struct has_is_always_equal<T, typename make_void<typename T::is_always_equal>::type> : std::true_type
		{
		};

		/// Provide a is_always_equal type traits for allocators in case current compiler
		/// std::allocator_traits::is_always_equal is not present.
		template<class Alloc, bool HasIsAlwaysEqual = has_is_always_equal<Alloc>::value>
		struct is_always_equal
		{
			using equal = typename std::allocator_traits<Alloc>::is_always_equal;
			static constexpr bool value = equal::value;
		};
		template<class Alloc>
		struct is_always_equal<Alloc, false>
		{
			static constexpr bool value = std::is_empty<Alloc>::value;
		};
	}

	/// @brief Copy allocator for container copy constructor
	template<class Allocator>
	auto copy_allocator(const Allocator& alloc) noexcept(std::is_nothrow_copy_constructible<Allocator>::value) -> Allocator
	{
		return std::allocator_traits<Allocator>::select_on_container_copy_construction(alloc);
	}

	/// @brief Swap allocators for container.swap member
	template<class Allocator>
	void swap_allocator(Allocator& left,
			    Allocator& right) noexcept(!std::allocator_traits<Allocator>::propagate_on_container_swap::value || std::allocator_traits<Allocator>::is_always_equal::value)
	{
		if STENOS_CONSTEXPR (std::allocator_traits<Allocator>::propagate_on_container_swap::value) {
			std::swap(left, right);
		}
		else {
			STENOS_ASSERT_DEBUG(left == right, "containers incompatible for swap");
		}
	}

	/// @brief Assign allocator for container copy operator
	template<class Allocator>
	void assign_allocator(Allocator& left,
			      const Allocator& right) noexcept(!std::allocator_traits<Allocator>::propagate_on_container_copy_assignment::value || std::is_nothrow_copy_assignable<Allocator>::value)
	{
		if STENOS_CONSTEXPR (std::allocator_traits<Allocator>::propagate_on_container_copy_assignment::value) {
			left = right;
		}
	}

	/// @brief Move allocator for container move assignment
	template<class Allocator>
	void move_allocator(Allocator& left,
			    Allocator& right) noexcept(!std::allocator_traits<Allocator>::propagate_on_container_move_assignment::value || std::is_nothrow_move_assignable<Allocator>::value)
	{
		// (maybe) propagate on container move assignment
		if STENOS_CONSTEXPR (std::allocator_traits<Allocator>::propagate_on_container_move_assignment::value) {
			left = std::move(right);
		}
	}

	// Returns whether an attempt to propagate allocators is necessary in copy assignment operations.
	// Note that even when false_type, callers should call assign_allocator as we want to assign allocators even when equal.
	template<class Allocator>
	struct assign_alloc
	{
		static constexpr bool value = std::allocator_traits<Allocator>::propagate_on_container_copy_assignment::value && !detail::is_always_equal<Allocator>::value;
	};

	template<class Allocator>
	struct move_alloc
	{
		static constexpr bool value = std::allocator_traits<Allocator>::propagate_on_container_move_assignment::type && !detail::is_always_equal<Allocator>::value;
	};

	/// @brief Returns the distance between first and last iterators for random access iterator category, 0 otherwise.
	template<class Iter>
	auto distance(const Iter& first, const Iter& last) noexcept -> size_t
	{
		return detail::iter_distance(first, last, typename std::iterator_traits<Iter>::iterator_category());
	}

	template<class T, class... Args>
	STENOS_ALWAYS_INLINE T* construct_ptr(T* p, Args&&... args)
	{
		return new (p) T(std::forward<Args>(args)...);
	}
	template<class T>
	STENOS_ALWAYS_INLINE void destroy_ptr(T* p) noexcept
	{
		p->~T();
	}

	/// @brief Convenient random access iterator on a constant value
	template<class T>
	class cvalue_iterator
	{
		using alloc_traits = std::allocator_traits<std::allocator<T>>;

	public:
		using iterator_category = std::random_access_iterator_tag;
		using value_type = T;
		using difference_type = typename alloc_traits::difference_type;
		using size_type = typename alloc_traits::size_type;
		using pointer = typename alloc_traits::const_pointer;
		using reference = const value_type&;

		explicit cvalue_iterator(size_type _pos) noexcept
		  : pos(_pos)
		{
		}
		cvalue_iterator(size_type _pos, const T& _value) noexcept
		  : pos(_pos)
		  , value(_value)
		{
		}

		auto operator*() const noexcept -> reference { return value; }
		auto operator->() const noexcept -> pointer { return std::pointer_traits<pointer>::pointer_to(**this); }
		auto operator++() noexcept -> cvalue_iterator&
		{
			++pos;
			return *this;
		}
		auto operator++(int) noexcept -> cvalue_iterator
		{
			cvalue_iterator _Tmp = *this;
			++(*this);
			return _Tmp;
		}
		auto operator--() noexcept -> cvalue_iterator&
		{
			// TODO(VM213788): check decrement
			--pos;
			return *this;
		}
		auto operator--(int) noexcept -> cvalue_iterator
		{
			cvalue_iterator _Tmp = *this;
			--(*this);
			return _Tmp;
		}
		auto operator==(const cvalue_iterator& other) const noexcept -> bool { return pos == other.pos; }
		auto operator!=(const cvalue_iterator& other) const noexcept -> bool { return pos != other.pos; }
		auto operator+=(difference_type diff) noexcept -> cvalue_iterator&
		{
			pos += diff;
			return *this;
		}
		auto operator-=(difference_type diff) noexcept -> cvalue_iterator&
		{
			pos -= diff;
			return *this;
		}
		auto operator[](difference_type diff) const noexcept -> const value_type& { return value; }

		T value;
		size_type pos;
	};

	template<class T>
	auto operator+(const cvalue_iterator<T>& it, typename cvalue_iterator<T>::difference_type diff) noexcept -> cvalue_iterator<T>
	{
		cvalue_iterator<T> res = it;
		return res += diff;
	}
	template<class T>
	auto operator-(const cvalue_iterator<T>& it, typename cvalue_iterator<T>::difference_type diff) noexcept -> cvalue_iterator<T>
	{
		cvalue_iterator<T> res = it;
		return res -= diff;
	}
	template<class T>
	auto operator-(const cvalue_iterator<T>& it1, const cvalue_iterator<T>& it2) noexcept -> typename cvalue_iterator<T>::difference_type
	{
		return it1.pos - it2.pos;
	}
	template<class T>
	bool operator<(const cvalue_iterator<T>& it1, const cvalue_iterator<T>& it2) noexcept
	{
		return it1.pos < it2.pos;
	}
	template<class T>
	bool operator>(const cvalue_iterator<T>& it1, const cvalue_iterator<T>& it2) noexcept
	{
		return it1.pos > it2.pos;
	}
	template<class T>
	bool operator<=(const cvalue_iterator<T>& it1, const cvalue_iterator<T>& it2) noexcept
	{
		return it1.pos <= it2.pos;
	}
	template<class T>
	bool operator>=(const cvalue_iterator<T>& it1, const cvalue_iterator<T>& it2) noexcept
	{
		return it1.pos >= it2.pos;
	}

	namespace detail
	{
		// Shared spinlock implementation
		class SharedSpinner
		{
			using lock_type = unsigned;
			static constexpr lock_type write = 1;
			static constexpr lock_type need_lock = 2;
			static constexpr lock_type read = 4;
			static constexpr lock_type max_read_mask = 1ull << (sizeof(lock_type) * 8u - 1u);

			bool failed_lock(lock_type& expect) noexcept
			{
				if (!(expect & (need_lock)))
					d_lock.fetch_or(need_lock, std::memory_order_release);
				expect = need_lock;
				return false;
			}
			STENOS_ALWAYS_INLINE bool try_lock(lock_type& expect) noexcept
			{
				if (!d_lock.compare_exchange_strong(expect, write, std::memory_order_acq_rel)) {
					return failed_lock(expect);
				}
				return true;
			}

			std::atomic<lock_type> d_lock;

		public:
			constexpr SharedSpinner() noexcept
			  : d_lock(0)
			{
			}
			SharedSpinner(SharedSpinner const&) = delete;
			SharedSpinner& operator=(SharedSpinner const&) = delete;

			STENOS_ALWAYS_INLINE void swap(SharedSpinner& other) noexcept
			{
				auto val = d_lock.load(std::memory_order_relaxed);
				d_lock.store(other.d_lock.load(std::memory_order_relaxed));
				other.d_lock.store(val);
			}

			STENOS_ALWAYS_INLINE void lock() noexcept
			{
				lock_type expect = 0;
				while (!try_lock(expect))
					std::this_thread::yield();
			}
			STENOS_ALWAYS_INLINE void unlock() noexcept
			{
				STENOS_ASSERT_DEBUG(d_lock & write, "");
				d_lock.fetch_and(static_cast<lock_type>(~(write | need_lock)), std::memory_order_release);
			}
			STENOS_ALWAYS_INLINE void lock_shared() noexcept
			{
				while (!try_lock_shared())
					std::this_thread::yield();
			}
			STENOS_ALWAYS_INLINE void unlock_shared() noexcept
			{
				STENOS_ASSERT_DEBUG(d_lock > 0, "");
				d_lock.fetch_sub(read, std::memory_order_release);
			}
			// Attempt to acquire writer permission. Return false if we didn't get it.
			STENOS_ALWAYS_INLINE bool try_lock() noexcept
			{
				if (d_lock.load(std::memory_order_relaxed) & (need_lock | write))
					return false;
				lock_type expect = 0;
				return d_lock.compare_exchange_strong(expect, write, std::memory_order_acq_rel);
			}
			STENOS_ALWAYS_INLINE bool try_lock_shared() noexcept
			{
				// This version might be slightly slower in some situations (low concurrency).
				// However it works for very small lock type (like uint8_t) by avoiding overflows.
				lock_type content = d_lock.load(std::memory_order_relaxed);
				return (!(content & (need_lock | write | max_read_mask)) && d_lock.compare_exchange_strong(content, content + read));
			}

			STENOS_ALWAYS_INLINE lock_type value() const noexcept { return d_lock.load(); }
		};

		template<class Ret>
		struct ResultOf
		{
			template<class F, class... Args>
			static STENOS_ALWAYS_INLINE bool apply(F&& f, Args... args)
			{
				return std::forward<F>(f)(std::forward<Args>(args)...);
			}
		};
		template<>
		struct ResultOf<void>
		{
			template<class F, class... Args>
			static STENOS_ALWAYS_INLINE bool apply(F&& f, Args&&... args)
			{
				std::forward<F>(f)(std::forward<Args>(args)...);
				return true;
			}
		};
		template<class F, class... Args>
		STENOS_ALWAYS_INLINE bool eval_functor(F&& f, Args&&... args)
		{
			using ret = decltype(f(std::declval<Args>()...));
			return ResultOf<ret>::apply(std::forward<F>(f), std::forward<Args>(args)...);
		}

		/// @brief Base class for RawBuffer to provide intrusive list features
		struct Iterator
		{
			Iterator* left;
			Iterator* right;

			void erase() noexcept
			{
				// remove from linked list
				left->right = right;
				right->left = left;
			}
			void insert(Iterator* _left, Iterator* _right) noexcept
			{
				// insert in linked list
				this->left = _left;
				this->right = _right;
				_left->right = this;
				_right->left = this;
			}
		};

		/// @brief Raw buffer used to compress/decompress blocks
		template<class T, unsigned block_size>
		struct RawBuffer : public Iterator
		{
			static constexpr size_t storage_size = block_size * sizeof(T);
			static constexpr size_t invalid_index = std::numeric_limits<size_t>::max();

			unsigned size;					// size is used for front and back buffer
			volatile unsigned dirty;			// dirty (modified) buffer, use volatile instead of atomic (slightly better performances,
									// and works all the time since we have an aligned 32 bit variable).
			size_t block_index;				// block index in list of blocks
			char* buffer;					// compressed buffer
			alignas(alignof(T)) char storage[storage_size]; // data storage, aligned on 16 bytes at least

			STENOS_ALWAYS_INLINE void mark_dirty() noexcept
			{
				// Mark as dirty with relaxed ordering.
				// The goal is to mark as dirty, we don't care
				// which value does it first.
				//
				// Note that a buffer can only be marked as dirty
				// while NOY being reused for another bucket since
				// mark_dirty() is only called when the bucket is locked.

				// if (!dirty.load(std::memory_order_relaxed))
				//	dirty.store(1, std::memory_order_relaxed);
				dirty = 1;
			}
			STENOS_ALWAYS_INLINE void mark_not_dirty() noexcept { dirty = 0; }

			template<class CompressVec>
			STENOS_ALWAYS_INLINE void mark_dirty(CompressVec* vec) noexcept
			{
				// mark as dirty and release related compressed memory (if any)
				mark_dirty();
				// release memory
				if (block_index != invalid_index)
					vec->dealloc_bucket(block_index);
			}

			void clear_values() noexcept
			{
				// Destroy values
				if (!std::is_trivially_destructible<T>::value) {
					for (size_t i = 0; i < size; ++i) {
						at(i).~T();
					}
				}
				// reset dirty and size
				mark_not_dirty();
				size = 0;
			}

			void reset() noexcept
			{
				size = 0;
				dirty = 0;
				block_index = invalid_index;
				buffer = nullptr;
			}

			STENOS_ALWAYS_INLINE auto data() noexcept -> T* { return reinterpret_cast<T*>(storage); }
			STENOS_ALWAYS_INLINE auto data() const noexcept -> const T* { return reinterpret_cast<const T*>(storage); }
			STENOS_ALWAYS_INLINE auto at(size_t index) noexcept -> T& { return data()[index]; }
			STENOS_ALWAYS_INLINE auto at(size_t index) const noexcept -> const T& { return data()[index]; }
		};

		/// @brief Intrusive linked list of RawBuffer
		template<class Buffer>
		struct BufferList
		{
			struct iterator
			{
				Iterator* it;
				iterator(Iterator* i = nullptr) noexcept
				  : it(i)
				{
				}
				auto operator++() noexcept -> iterator&
				{
					it = it->right;
					return *this;
				}
				auto operator++(int) noexcept -> iterator
				{
					iterator _Tmp = *this;
					++(*this);
					return _Tmp;
				}
				auto operator--() noexcept -> iterator&
				{
					it = it->left;
					return *this;
				}
				auto operator--(int) noexcept -> iterator
				{
					iterator _Tmp = *this;
					--(*this);
					return _Tmp;
				}
				auto operator*() const noexcept -> Buffer* { return static_cast<Buffer*>(it); }
				bool operator==(const iterator& other) const noexcept { return it == other.it; }
				bool operator!=(const iterator& other) const noexcept { return it != other.it; }
			};

			Iterator d_end;
			size_t d_size;

			BufferList() noexcept
			  : d_size(0)
			{
				d_end.left = d_end.right = &d_end;
			}
			auto size() const noexcept -> size_t { return d_size; }
			auto begin() noexcept -> iterator { return iterator(d_end.right); }
			auto end() noexcept -> iterator { return iterator(&d_end); }
			void assign(BufferList&& other) noexcept
			{
				auto l = other.d_end.left;
				auto r = other.d_end.right;
				d_end = other.d_end;
				d_size = other.d_size;
				if (d_size) {
					l->right = &d_end;
					r->left = &d_end;
				}
				else
					d_end.left = d_end.right = &d_end;
			}
			void clear() noexcept
			{
				d_size = 0;
				d_end.left = d_end.right = &d_end;
			}
			void push_back(Buffer* b) noexcept
			{
				++d_size;
				b->insert(d_end.left, &d_end);
			}
			void push_front(Buffer* b) noexcept
			{
				++d_size;
				b->insert(&d_end, d_end.right);
			}
			void pop_back() noexcept
			{
				--d_size;
				back()->erase();
			}
			void pop_front() noexcept
			{
				--d_size;
				front()->erase();
			}
			void erase(Buffer* b) noexcept
			{
				--d_size;
				b->erase();
			}
			void erase(iterator it) noexcept { erase(*it); }
			auto back() noexcept -> Buffer* { return static_cast<Buffer*>(d_end.left); }
			auto front() noexcept -> Buffer* { return static_cast<Buffer*>(d_end.right); }
		};

		enum BufferType : uintptr_t
		{
			Raw = 0,
			Compressed = 1,
		};

		// Tag pointer of either compressed block (buffer) or RawBuffer
		template<class T, unsigned block_size>
		class TagPointer
		{
			volatile uintptr_t d_ptr = 0; // Not certain volatile is mandatory
			STENOS_ALWAYS_INLINE uintptr_t load() const noexcept { return d_ptr; }

		public:
			constexpr TagPointer() = default;
			STENOS_ALWAYS_INLINE TagPointer(void* p, BufferType type) noexcept
			  : d_ptr((uintptr_t)p | type)
			{
			}
			STENOS_ALWAYS_INLINE RawBuffer<T, block_size>* raw() const noexcept { return (d_ptr & Compressed) ? nullptr : reinterpret_cast<RawBuffer<T, block_size>*>(d_ptr); }
			STENOS_ALWAYS_INLINE char* compressed() const noexcept { return (d_ptr & Compressed) ? reinterpret_cast<char*>(d_ptr & ~1ull) : nullptr; }
			STENOS_ALWAYS_INLINE char* find_compressed() const noexcept
			{
				return d_ptr ? ((d_ptr & Compressed) ? reinterpret_cast<char*>(d_ptr & ~1ull) : reinterpret_cast<RawBuffer<T, block_size>*>(d_ptr)->buffer) : nullptr;
			}
			STENOS_ALWAYS_INLINE void set(void* p, BufferType type) noexcept { d_ptr = (uintptr_t)p | type; }
			STENOS_ALWAYS_INLINE void swap(TagPointer& other) noexcept { std::swap(d_ptr, other.d_ptr); }
			STENOS_ALWAYS_INLINE operator bool() const noexcept { return d_ptr != 0; }
		};

		/// @brief Compressed buffer class
		template<class T, unsigned block_size>
		struct PackBuffer
		{
			// Pointer to decompressed buffer, if any
			TagPointer<T, block_size> data;

			// Compressed size
			unsigned csize;

			// Bucket reference count.
			// Should be held in shared mode for buffer access,
			// in unique mode to be reused for another bucket.
			SharedSpinner ref_count;

			STENOS_ALWAYS_INLINE auto load_decompressed() const noexcept { return data.raw(); }

			PackBuffer(RawBuffer<T, block_size>* dec = nullptr, char* buff = nullptr, unsigned _csize = 0) noexcept
			  : csize(_csize)
			{
				if (dec) {
					data.set(dec, Raw);
					dec->buffer = buff;
				}
				else
					data.set(buff, Compressed);
			}

			// Move semantic for usage inside std::vector
			PackBuffer(PackBuffer&& other) noexcept
			  : csize(other.csize)

			{
				other.csize = 0;
				data.swap(other.data);
				ref_count.swap(other.ref_count);
			}
			PackBuffer& operator=(PackBuffer&& other) noexcept
			{
				// Swap decompression buffer
				data.swap(other.data);

				// Swap reference count.
				// This is mandatory if the buckt array grows
				// while we still have RefWrapper objects
				// pointing to the vector.
				ref_count.swap(other.ref_count);

				std::swap(csize, other.csize);
				return *this;
			}

			STENOS_ALWAYS_INLINE PackBuffer& get() const noexcept { return *const_cast<PackBuffer*>(this); }
			STENOS_ALWAYS_INLINE void ref() noexcept { ref_count.lock_shared(); }
			STENOS_ALWAYS_INLINE void unref() noexcept { ref_count.unlock_shared(); }
		};

		///@brief Create a raw buffer aligned on 16 bytes
		template<class T, unsigned block_size, class Alloc>
		static auto make_raw_buffer(Alloc al) -> RawBuffer<T, block_size>*
		{
			RawBuffer<T, block_size>* res = reinterpret_cast<RawBuffer<T, block_size>*>(al.allocate(sizeof(RawBuffer<T, block_size>)));
			res->size = 0;
			res->dirty = 0;
			res->block_index = 0;
			res->buffer = nullptr;
			res->left = res->right = nullptr;
			return res;
		}

		template<class T, unsigned block_size>
		static auto get_raw_buffer() noexcept -> RawBuffer<T, block_size>*
		{
			// Returns a RawBuffer suitable to destroy a compressed buffer.
			// Wont be used in most cases.
			static thread_local RawBuffer<T, block_size> buff;
			return &buff;
		}

		// Forward declarations

		template<class U>
		class CompressedConstIter;

		template<class U>
		class CompressedIter;

		template<class Compressed>
		class RefWrapper;

		template<class Compressed>
		class ConstRefWrapper;

		// Base class for ValueWrapper, ConstRefWrapper and RefWrapper
		template<class Derived>
		class BaseValue
		{
		public:
			STENOS_ALWAYS_INLINE const auto& get() const { return (static_cast<const Derived&>(*this)).get(); }
		};

		// Value wrapper, used as 
		/* template<class T>
		class ValueWrapper : public BaseValue<ValueWrapper<T>>
		{
			T value;

		public:
			using value_type = T;
			ValueWrapper() = default;
			~ValueWrapper() = default;
			STENOS_ALWAYS_INLINE ValueWrapper(const T& v)
			  : value(v)
			{
			}
			STENOS_ALWAYS_INLINE ValueWrapper(T&& v) noexcept
			  : value(std::move(v))
			{
			}
			STENOS_ALWAYS_INLINE ValueWrapper(const ValueWrapper& other)
			  : value(other.value)
			{
			}
			STENOS_ALWAYS_INLINE ValueWrapper(ValueWrapper&& other) noexcept
			  : value(std::move(other.value))
			{
			}
			template<class C>
			ValueWrapper(const ConstRefWrapper<C>& v);
			template<class C>
			ValueWrapper(RefWrapper<C>&& v) noexcept;

			STENOS_ALWAYS_INLINE ValueWrapper& operator=(const T& v)
			{
				value = v;
				return *this;
			}
			STENOS_ALWAYS_INLINE ValueWrapper& operator=(T&& v) noexcept
			{
				value = std::move(v);
				return *this;
			}
			STENOS_ALWAYS_INLINE ValueWrapper& operator=(const ValueWrapper& v)
			{
				value = v.value;
				return *this;
			}
			STENOS_ALWAYS_INLINE ValueWrapper& operator=(ValueWrapper&& v) noexcept
			{
				value = std::move(v.value);
				return *this;
			}
			template<class C>
			ValueWrapper& operator=(const ConstRefWrapper<C>& v);
			template<class C>
			ValueWrapper& operator=(RefWrapper<C>&& v) noexcept;

			STENOS_ALWAYS_INLINE const T& get() const { return value; }
			STENOS_ALWAYS_INLINE operator T&() noexcept { return value; }
			STENOS_ALWAYS_INLINE operator const T&() const noexcept { return value; }
		};*/

		/// @brief Const value wrapper class for cvector and cvector::iterator
		template<class Compressed>
		class ConstRefWrapper : public BaseValue<ConstRefWrapper<Compressed>>
		{
			friend class CompressedConstIter<Compressed>;
			friend class CompressedIter<Compressed>;
			friend class RefWrapper<Compressed>;

		protected:
			using BucketType = typename Compressed::BucketType;

			Compressed* c;
			size_t bucket;
			size_t bpos;

			STENOS_ALWAYS_INLINE auto _bucket() const noexcept -> const BucketType* { return &_c()->d_buckets[bucket]; }
			STENOS_ALWAYS_INLINE auto _bucket() noexcept -> BucketType* { return &_c()->d_buckets[bucket]; }
			STENOS_ALWAYS_INLINE auto _c() const noexcept -> Compressed* { return const_cast<Compressed*>(c); }
			STENOS_ALWAYS_INLINE auto decompress_if_needed(size_t exclude = static_cast<size_t>(-1)) const
			{
				auto decompressed = this->_bucket()->load_decompressed();
				if (!decompressed)
					decompressed = _c()->decompress_bucket(bucket, exclude);
				return decompressed;
			}

			STENOS_ALWAYS_INLINE bool move_inside_block(std::ptrdiff_t val)
			{
				// Increment position within a block, returns true if we stay on the block
				bpos += val;
				return (bpos < Compressed::elems_per_block);
			}

		public:
			using value_type = typename Compressed::value_type;
			using T = value_type;
			using reference = const T&;
			using const_reference = const T&;

			STENOS_ALWAYS_INLINE ConstRefWrapper(const Compressed* _c, size_t b, size_t pos) noexcept
			  : c(const_cast<Compressed*>(_c))
			  , bucket(b)
			  , bpos(pos)
			{
				_bucket()->ref();
			}
			STENOS_ALWAYS_INLINE ConstRefWrapper(const Compressed* _c, size_t b, size_t pos, bool) noexcept
			  : c(const_cast<Compressed*>(_c))
			  , bucket(b)
			  , bpos(pos)
			{
				// Version that do NOT increment the bucket ref count
			}

			STENOS_ALWAYS_INLINE ConstRefWrapper(const ConstRefWrapper& other) noexcept
			  : c(other.c)
			  , bucket(other.bucket)
			  , bpos(other.bpos)
			{
				_bucket()->ref();
			}
			STENOS_ALWAYS_INLINE ~ConstRefWrapper() noexcept { _bucket()->unref(); }

			STENOS_ALWAYS_INLINE auto bucket_index() const noexcept -> size_t { return bucket; }
			STENOS_ALWAYS_INLINE auto bucket_pos() const noexcept -> size_t { return bpos; }
			STENOS_ALWAYS_INLINE auto vector_data() const noexcept -> const void* { return c; }

			STENOS_ALWAYS_INLINE auto get() const -> const T& { return this->decompress_if_needed()->at(bpos); }

			STENOS_ALWAYS_INLINE operator const T&() const { return get(); }
		};

		template<class Wrapper, class T = typename Wrapper::value_type, bool IsMoveOnly = (!std::is_copy_constructible<T>::value && std::is_move_constructible<T>::value)>
		struct ConversionWrapper
		{
			using type = T&&;
			static STENOS_ALWAYS_INLINE T&& move(Wrapper& w) { return std::move(w.move()); }
		};
		template<class Wrapper, class T>
		struct ConversionWrapper<Wrapper, T, false>
		{
			using type = const T&;
			static STENOS_ALWAYS_INLINE const T& move(Wrapper& w) { return w.get(); }
		};

		template<class Compressed>
		class RefWrapper : public ConstRefWrapper<Compressed>
		{
			using conv = ConversionWrapper<RefWrapper<Compressed>, typename Compressed::value_type>;
			using conv_type = typename conv::type;

		public:
			using T = typename ConstRefWrapper<Compressed>::T;
			using base_type = ConstRefWrapper<Compressed>;
			using BucketType = typename base_type::BucketType;
			using value_type = T;
			using reference = T&;
			using const_reference = const T&;
			using base_type::get;

			STENOS_ALWAYS_INLINE RefWrapper(const Compressed* _c, size_t b, size_t pos) noexcept
			  : base_type(_c, b, pos)
			{
			}
			STENOS_ALWAYS_INLINE RefWrapper(const Compressed* _c, size_t b, size_t pos, bool) noexcept
			  : base_type(_c, b, pos, false)
			{
			}
			STENOS_ALWAYS_INLINE RefWrapper(const RefWrapper& other) noexcept = default;

			STENOS_ALWAYS_INLINE auto move() noexcept -> T&&
			{
				auto raw = this->decompress_if_needed();
				if (!std::is_trivially_move_assignable<T>::value)
					raw->mark_dirty();
				return std::move(raw->at(this->bpos));
			}

			STENOS_ALWAYS_INLINE void set(const T& obj)
			{
				auto raw = this->decompress_if_needed();
				raw->at(this->bpos) = obj;
				raw->mark_dirty();
			}
			STENOS_ALWAYS_INLINE void set(T&& obj)
			{
				auto raw = this->decompress_if_needed();
				raw->at(this->bpos) = std::move(obj);
				raw->mark_dirty();
			}

			STENOS_ALWAYS_INLINE operator const T&() const { return this->get(); }
			STENOS_ALWAYS_INLINE operator conv_type() { return conv::move(*this); }

			STENOS_ALWAYS_INLINE auto operator=(const base_type& other) -> RefWrapper&
			{
				if STENOS_LIKELY (std::addressof(other) != this) {
					auto raw_this = this->decompress_if_needed(other.bucket);
					auto raw_other = other.decompress_if_needed(this->bucket);
					raw_this->mark_dirty();
					raw_this->at(this->bpos) = raw_other->at(other.bpos);
				}
				return *this;
			}
			STENOS_ALWAYS_INLINE auto operator=(RefWrapper&& other) -> RefWrapper&
			{
				if STENOS_LIKELY (std::addressof(other) != this) {
					auto raw_this = this->decompress_if_needed(other.bucket);
					auto raw_other = other.decompress_if_needed(this->bucket);
					if (!std::is_trivially_move_assignable<T>::value)
						raw_other->mark_dirty();
					raw_this->mark_dirty();
					raw_this->at(this->bpos) = std::move(raw_other->at(other.bpos));
				}
				return *this;
			}
			STENOS_ALWAYS_INLINE auto operator=(const T& other) -> RefWrapper&
			{
				set(other);
				return *this;
			}
			STENOS_ALWAYS_INLINE auto operator=(T&& other) -> RefWrapper&
			{
				set(std::move(other));
				return *this;
			}
			/* STENOS_ALWAYS_INLINE auto operator=(const ValueWrapper<T>& other) -> RefWrapper&
			{
				set(other.value);
				return *this;
			}
			STENOS_ALWAYS_INLINE auto operator=(ValueWrapper<T>&& other) -> RefWrapper&
			{
				set(std::move(static_cast<T&>(other)));
				return *this;
			}*/
			STENOS_ALWAYS_INLINE void swap(RefWrapper& other)
			{
				if STENOS_LIKELY (std::addressof(other) != this) {
					auto raw_this = this->decompress_if_needed(other.bucket);
					auto raw_other = other.decompress_if_needed(this->bucket);
					raw_this->mark_dirty();
					raw_other->mark_dirty();
					std::swap(raw_this->at(this->bpos), raw_other->at(other.bpos));
				}
			}
		};

		/* template<class T>
		template<class C>
		STENOS_ALWAYS_INLINE ValueWrapper<T>::ValueWrapper(const ConstRefWrapper<C>& v)
		  : value(v.get())
		{
		}
		template<class T>
		template<class C>
		STENOS_ALWAYS_INLINE ValueWrapper<T>::ValueWrapper(RefWrapper<C>&& v) noexcept
		  : value(std::move(v.move()))
		{
		}

		template<class T>
		template<class C>
		STENOS_ALWAYS_INLINE ValueWrapper<T>& ValueWrapper<T>::operator=(const ConstRefWrapper<C>& v)
		{
			value = v.get();
			return *this;
		}
		template<class T>
		template<class C>
		STENOS_ALWAYS_INLINE ValueWrapper<T>& ValueWrapper<T>::operator=(RefWrapper<C>&& v) noexcept
		{
			value = std::move(v.move());
			return *this;
		}*/

		// Overload swap function for RefWrapper

		template<class Compressed>
		STENOS_ALWAYS_INLINE void swap(RefWrapper<Compressed>&& a, RefWrapper<Compressed>&& b) noexcept
		{
			using wrapper = RefWrapper<Compressed>;
			const_cast<wrapper&>(a).swap(const_cast<wrapper&>(b));
		}
		template<class Compressed>
		STENOS_ALWAYS_INLINE void swap(RefWrapper<Compressed>& a, RefWrapper<Compressed>& b) noexcept
		{
			(a).swap((b));
		}

		// Operators overloads for BaseValue to make it work with most standard algorithms (like std::sort)

		template<class D1, class D2>
		bool operator==(const BaseValue<D1>& d1, const BaseValue<D2>& d2)
		{
			return d1.get() == d2.get();
		}
		template<class D1, class D2>
		bool operator!=(const BaseValue<D1>& d1, const BaseValue<D2>& d2)
		{
			return d1.get() != d2.get();
		}
		template<class D1, class D2>
		bool operator<(const BaseValue<D1>& d1, const BaseValue<D2>& d2)
		{
			return d1.get() < d2.get();
		}
		template<class D1, class D2>
		bool operator<=(const BaseValue<D1>& d1, const BaseValue<D2>& d2)
		{
			return d1.get() <= d2.get();
		}
		template<class D1, class D2>
		bool operator>(const BaseValue<D1>& d1, const BaseValue<D2>& d2)
		{
			return d1.get() > d2.get();
		}
		template<class D1, class D2>
		bool operator>=(const BaseValue<D1>& d1, const BaseValue<D2>& d2)
		{
			return d1.get() >= d2.get();
		}

		/// @brief const iterator type for cvector
		template<class Compressed>
		class CompressedConstIter
		{
			using ref_type = typename Compressed::ref_type;
			using const_ref_type = typename Compressed::const_ref_type;
			static constexpr size_t invalid_status = (size_t)-1;

			STENOS_ALWAYS_INLINE auto* as_ref() const { return reinterpret_cast<const_ref_type*>(data); }
			STENOS_ALWAYS_INLINE bool is_valid() const { return as_ref()->bucket != invalid_status; }
			STENOS_ALWAYS_INLINE void invalidate() noexcept
			{
				if (is_valid()) {
					// STENOS_ASSERT_DEBUG(as_ref()->_bucket()->used != 0, "");
					as_ref()->~const_ref_type();
					as_ref()->bucket = invalid_status;
				}
			}

		public:
			using T = typename Compressed::value_type;
			using value_type = T;//ValueWrapper<T>;
			using reference = ref_type;
			using const_reference = const_ref_type&;
			using pointer = const T*;
			using const_pointer = const T*;
			using difference_type = typename Compressed::difference_type;
			using iterator_category = std::random_access_iterator_tag;
			using size_type = size_t;
			static constexpr size_t elems_per_block = Compressed::elems_per_block;
			static constexpr size_t mask = Compressed::mask;
			static constexpr size_t shift = Compressed::shift;

			difference_type abspos;
			mutable char data[sizeof(const_ref_type)];

			STENOS_ALWAYS_INLINE Compressed* c() const { return as_ref()->_c(); }

			STENOS_ALWAYS_INLINE CompressedConstIter() noexcept
			  : abspos(0)
			{
				new (as_ref()) const_ref_type(nullptr, invalid_status, invalid_status, false);
			}
			STENOS_ALWAYS_INLINE CompressedConstIter(const Compressed* c, size_t pos) noexcept
			  : abspos((difference_type)pos)
			{
				new (as_ref()) const_ref_type(const_cast<Compressed*>(c), invalid_status, invalid_status, false);
			}

			STENOS_ALWAYS_INLINE CompressedConstIter(const CompressedConstIter& other)
			  : abspos(other.abspos)
			{
				new (as_ref()) const_ref_type(other.c(), invalid_status, invalid_status, false);
			}

			STENOS_ALWAYS_INLINE ~CompressedConstIter() noexcept { invalidate(); }

			STENOS_ALWAYS_INLINE CompressedConstIter& operator=(const CompressedConstIter& other) noexcept
			{
				abspos = (other.abspos);
				invalidate();
				new (as_ref()) const_ref_type(other.c(), invalid_status, invalid_status, false);
				return *this;
			}

			STENOS_ALWAYS_INLINE auto operator++() noexcept -> CompressedConstIter&
			{
				++abspos;
				if (is_valid() && as_ref()->move_inside_block(1))
					return *this;
				invalidate();
				return *this;
			}
			STENOS_ALWAYS_INLINE auto operator++(int) noexcept -> CompressedConstIter
			{
				CompressedConstIter it = *this;
				++(*this);
				return it;
			}
			STENOS_ALWAYS_INLINE auto operator--() noexcept -> CompressedConstIter&
			{
				--abspos;
				if (is_valid() && as_ref()->move_inside_block(-1))
					return *this;
				invalidate();
				return *this;
			}
			STENOS_ALWAYS_INLINE auto operator--(int) noexcept -> CompressedConstIter
			{
				CompressedConstIter it = *this;
				--(*this);
				return it;
			}

			STENOS_ALWAYS_INLINE auto operator*() const noexcept -> const_reference
			{
				STENOS_ASSERT_DEBUG(this->abspos >= 0 && this->abspos < static_cast<difference_type>(this->c()->size()), "attempt to dereference an invalid iterator");
				if (!is_valid())
					new (as_ref()) const_ref_type(c()->cat(static_cast<size_t>(abspos)));
				return *as_ref();
			}
			STENOS_ALWAYS_INLINE auto operator->() const noexcept -> const T* { return &((operator*()).get()); }
			STENOS_ALWAYS_INLINE auto operator+=(difference_type diff) noexcept -> CompressedConstIter&
			{
				this->abspos += diff;
				if (is_valid() && as_ref()->move_inside_block(diff))
					return *this;
				invalidate();
				return *this;
			}
			STENOS_ALWAYS_INLINE auto operator-=(difference_type diff) noexcept -> CompressedConstIter&
			{
				(*this) += -diff;
				return *this;
			}
			STENOS_ALWAYS_INLINE auto operator+(difference_type diff) const noexcept -> CompressedConstIter
			{
				CompressedConstIter tmp = *this;
				tmp += diff;
				return tmp;
			}
			STENOS_ALWAYS_INLINE auto operator-(difference_type diff) const noexcept -> CompressedConstIter
			{
				CompressedConstIter tmp = *this;
				tmp -= diff;
				return tmp;
			}
			STENOS_ALWAYS_INLINE const_ref_type operator[](difference_type diff) const noexcept { return c()->at(abspos + diff); }
		};

		/// @brief iterator type for cvector
		template<class Compressed>
		class CompressedIter : public CompressedConstIter<Compressed>
		{
		public:
			using this_type = CompressedIter<Compressed>;
			using base_type = CompressedConstIter<Compressed>;
			using ref_type = typename Compressed::ref_type;
			using const_ref_type = typename Compressed::const_ref_type;
			using value_type = T;//typename base_type::value_type;
			using reference = ref_type&;
			using const_reference = const_ref_type&;
			using pointer = value_type*;
			using const_pointer = const value_type*;
			using difference_type = typename Compressed::difference_type;
			using iterator_category = std::random_access_iterator_tag;
			using size_type = size_t;

			STENOS_ALWAYS_INLINE CompressedIter() noexcept
			  : base_type()
			{
			}
			STENOS_ALWAYS_INLINE CompressedIter(const base_type& other) noexcept
			  : base_type(other)
			{
			}
			STENOS_ALWAYS_INLINE CompressedIter(const CompressedIter& other) noexcept
			  : base_type(other)
			{
			}
			STENOS_ALWAYS_INLINE CompressedIter(const Compressed* c, difference_type p) noexcept
			  : base_type(c, p)
			{
			}
			STENOS_ALWAYS_INLINE CompressedIter& operator=(const CompressedIter& other) noexcept
			{
				base_type::operator=(static_cast<const base_type>(other));
				return *this;
			}
			STENOS_ALWAYS_INLINE auto operator*() const noexcept -> reference { return reinterpret_cast<reference>(const_cast<this_type*>(this)->base_type::operator*()); }
			STENOS_ALWAYS_INLINE auto operator->() const noexcept -> const_pointer { return base_type::operator->(); }
			STENOS_ALWAYS_INLINE auto operator++() noexcept -> CompressedIter&
			{
				base_type::operator++();
				return *this;
			}
			STENOS_ALWAYS_INLINE auto operator++(int) noexcept -> CompressedIter
			{
				CompressedIter _Tmp = *this;
				base_type::operator++();
				return _Tmp;
			}
			STENOS_ALWAYS_INLINE auto operator--() noexcept -> CompressedIter&
			{
				base_type::operator--();
				return *this;
			}
			STENOS_ALWAYS_INLINE auto operator--(int) noexcept -> CompressedIter
			{
				CompressedIter _Tmp = *this;
				base_type::operator--();
				return _Tmp;
			}
			STENOS_ALWAYS_INLINE auto operator+=(difference_type diff) noexcept -> CompressedIter&
			{
				base_type::operator+=(diff);
				return *this;
			}
			STENOS_ALWAYS_INLINE auto operator-=(difference_type diff) noexcept -> CompressedIter&
			{
				base_type::operator-=(diff);
				return *this;
			}
			STENOS_ALWAYS_INLINE auto operator+(difference_type diff) const noexcept -> CompressedIter
			{
				CompressedIter tmp = *this;
				tmp += diff;
				return tmp;
			}
			STENOS_ALWAYS_INLINE auto operator-(difference_type diff) const noexcept -> CompressedIter
			{
				CompressedIter tmp = *this;
				tmp -= diff;
				return tmp;
			}
			STENOS_ALWAYS_INLINE ref_type operator[](difference_type diff) const noexcept { return const_cast<Compressed*>(this->c())->at(this->abspos + diff); }
		};

		template<class C>
		STENOS_ALWAYS_INLINE auto operator-(const CompressedConstIter<C>& a, const CompressedConstIter<C>& b) noexcept -> typename CompressedConstIter<C>::difference_type
		{
			STENOS_ASSERT_DEBUG(a.c() == b.c() || a.c() == nullptr || b.c() == nullptr, "comparing iterators from different containers");
			return a.abspos - b.abspos;
		}

		template<class C>
		STENOS_ALWAYS_INLINE bool operator==(const CompressedConstIter<C>& a, const CompressedConstIter<C>& b) noexcept
		{
			STENOS_ASSERT_DEBUG(a.c() == b.c() || a.c() == nullptr || b.c() == nullptr, "comparing iterators from different containers");
			return a.abspos == b.abspos;
		}
		template<class C>
		STENOS_ALWAYS_INLINE bool operator!=(const CompressedConstIter<C>& a, const CompressedConstIter<C>& b) noexcept
		{
			STENOS_ASSERT_DEBUG(a.c() == b.c() || a.c() == nullptr || b.c() == nullptr, "comparing iterators from different containers");
			return a.abspos != b.abspos;
		}
		template<class C>
		STENOS_ALWAYS_INLINE bool operator<(const CompressedConstIter<C>& a, const CompressedConstIter<C>& b) noexcept
		{
			STENOS_ASSERT_DEBUG(a.c() == b.c() || a.c() == nullptr || b.c() == nullptr, "comparing iterators from different containers");
			return a.abspos < b.abspos;
		}
		template<class C>
		STENOS_ALWAYS_INLINE bool operator>(const CompressedConstIter<C>& a, const CompressedConstIter<C>& b) noexcept
		{
			STENOS_ASSERT_DEBUG(a.c() == b.c() || a.c() == nullptr || b.c() == nullptr, "comparing iterators from different containers");
			return a.abspos > b.abspos;
		}
		template<class C>
		STENOS_ALWAYS_INLINE bool operator<=(const CompressedConstIter<C>& a, const CompressedConstIter<C>& b) noexcept
		{
			STENOS_ASSERT_DEBUG(a.c() == b.c() || a.c() == nullptr || b.c() == nullptr, "comparing iterators from different containers");
			return a.abspos <= b.abspos;
		}
		template<class C>
		STENOS_ALWAYS_INLINE bool operator>=(const CompressedConstIter<C>& a, const CompressedConstIter<C>& b) noexcept
		{
			STENOS_ASSERT_DEBUG(a.c() == b.c() || a.c() == nullptr || b.c() == nullptr, "comparing iterators from different containers");
			return a.abspos >= b.abspos;
		}

		template<unsigned block_bytes>
		static STENOS_ALWAYS_INLINE void* get_compression_buffer() noexcept
		{
			static constexpr size_t buf_size = compress_bound(block_bytes);
			thread_local uint8_t buffer[buf_size];
			return buffer;
		}

		/// @brief Internal structure used by cvector that gathers all the container logics
		///
		template<class T, class Allocator, unsigned Shift = 0, int Level = 1>
		struct CompressedVectorInternal : private Allocator
		{
			static constexpr size_t block_size = (256 << Shift);
			using ThisType = CompressedVectorInternal<T, Allocator, Shift, Level>;

			using value_type = T;
			using allocator_type = Allocator;
			using reference = T&;
			using const_reference = const T&;
			using difference_type = typename std::allocator_traits<Allocator>::difference_type;
			using pointer = T*;
			using const_pointer = const T*;
			using iterator = CompressedIter<ThisType>;
			using const_iterator = CompressedConstIter<ThisType>;
			using ref_type = RefWrapper<ThisType>;
			using const_ref_type = ConstRefWrapper<ThisType>;
			using BucketType = PackBuffer<T, block_size>;
			using RawType = RawBuffer<T, block_size>;

			// Rebind allocator
			template<class U>
			using RebindAlloc = typename std::allocator_traits<Allocator>::template rebind_alloc<U>;
			// List of decompression contexts
			using ContextType = BufferList<RawType>;

			// block size
			static constexpr size_t elems_per_block = block_size;
			// Mask for random access
			static constexpr size_t mask = elems_per_block - 1;
			// Shift for random access
			static constexpr size_t shift = 8 + Shift;
			// Level value, 0 or 1
			static constexpr int level = Level;

			static constexpr size_t block_bytes = block_size * sizeof(T);
			static constexpr size_t dst_block_bytes = compress_bound(block_bytes);

			std::vector<BucketType, RebindAlloc<BucketType>> d_buckets; // compressed buckets
			ContextType d_contexts;					    // decompression contexts
			size_t d_size;						    // number of values
			SharedSpinner d_lock;					    // global Spinlock
			SharedSpinner d_ctx_lock;
			stenos_context* d_ctx = stenos_make_context();

			STENOS_ALWAYS_INLINE void check_destroy_bucket(size_t idx) noexcept
			{
				// Ensure we can lock the bucket to avoid dangling references
				if (d_buckets[idx].ref_count.value() != 0)
					STENOS_ABORT("About to create a dangling reference!");
			}

			STENOS_ALWAYS_INLINE void* compression_buffer() noexcept { return get_compression_buffer<block_bytes>(); }

			STENOS_ALWAYS_INLINE size_t compress(const void* in, size_t bytes = 0) noexcept
			{
				std::lock_guard<SharedSpinner> lock(d_ctx_lock);
				size_t r = stenos_private_compress_block(d_ctx, in, sizeof(T), block_bytes, bytes ? bytes : block_bytes, compression_buffer(), dst_block_bytes);
				if (stenos_has_error(r))
					STENOS_ABORT("cvector: abort on compression error") // no way to recover from this
#ifndef NDEBUG
				char out[block_bytes];
				size_t r2 = stenos_private_decompress_block(d_ctx, compression_buffer(), sizeof(T), block_bytes, r, out, sizeof(out));
				STENOS_ASSERT_DEBUG(r2 == bytes ? bytes : block_bytes, "");
				STENOS_ASSERT_DEBUG(memcmp(in, out, bytes ? bytes : block_bytes) == 0, "");
#endif
				return r;
			}

			STENOS_ALWAYS_INLINE size_t decompress(BucketType* pack, void* dst) noexcept
			{
				if (pack->csize == 0)
					return 0;

				std::lock_guard<SharedSpinner> lock(d_ctx_lock);
				size_t r = stenos_private_decompress_block(d_ctx, pack->data.find_compressed(), sizeof(T), block_bytes, pack->csize, dst, block_bytes);
				if (stenos_has_error(r) || r != block_bytes)
					STENOS_ABORT("cvector: abort on decompression error")
				return r;
			}

			///@brief Destroy and deallocate a pack buffer.
			/// Also destroy and deallocate the uncompressed data.
			template<class Alloc>
			void destroy_pack_buffer(BucketType* pack, RawType* tmp, Alloc al) noexcept
			{
				// Here, tmp is just used to decompress and destroy values
				if (pack && pack->data) {
					char* buffer = pack->data.find_compressed();
					if (!std::is_trivially_destructible<T>::value) {
						if (auto raw = pack->load_decompressed()) {
							// destroy all values in  decompressed
							raw->clear_values();
						}
						else {
							// we must decompress first
							this->decompress(pack, tmp->storage);
							// destroy
							tmp->size = block_size;
							tmp->clear_values();
						}
					}
					al.deallocate(buffer, pack->csize);
				}
			}

		public:
			CompressedVectorInternal(const Allocator& al)
			  : Allocator(al)
			  , d_buckets(RebindAlloc<BucketType>(al))
			  , d_size(0)
			{
				if (!d_ctx)
					throw std::bad_alloc();
				stenos_set_level(d_ctx, level);
			}

			~CompressedVectorInternal() noexcept { clear(); }

			/// @brief Deallocate compressed memory for given bucket
			void dealloc_bucket(size_t index) noexcept
			{
				char* data = nullptr;
				unsigned csize = 0;
				{
					std::lock_guard<SharedSpinner> lock(d_lock);
					if (auto raw = d_buckets[index].load_decompressed()) {
						if (raw->block_index != RawType::invalid_index && raw->dirty && raw->buffer) {
							data = raw->buffer;
							csize = d_buckets[index].csize;
							d_buckets[index].csize = 0;
							raw->buffer = nullptr;
						}
					}
				}
				if (data)
					RebindAlloc<char>(*this).deallocate(data, csize);
			}

			/// @brief Clear content
			void clear() noexcept
			{
				RawType* tmp = nullptr;
				// First, try to find a valid context that we can reuse to destroy all compressed buffer
				if (d_buckets.size()) {
					for (auto it = d_contexts.begin(); it != d_contexts.end(); ++it) {

						if ((*it)->size == 0 || (*it)->size == elems_per_block) {
							tmp = (*it);

							// Destroy its content
							tmp->clear_values();
							break;
						}
					}
					if (!tmp)
						tmp = get_raw_buffer<T, block_size>();
				}

				// Destroy and free all compressed buckets
				for (size_t i = 0; i < d_buckets.size(); ++i) {

					// Ensure we can lock the bucket.
					// Otherwise, this means that we have a reference (RefWrapper)
					// pointing to it, and this will later cause a crash
					check_destroy_bucket(i);

					auto raw = d_buckets[i].load_decompressed();
					if (auto buf = d_buckets[i].data.find_compressed()) {
						if (raw != tmp)
							this->destroy_pack_buffer(&d_buckets[i], tmp, RebindAlloc<char>(*this));
						else
							RebindAlloc<char>(*this).deallocate(buf, d_buckets[i].csize);
					}
					else {
						if (raw && raw != tmp)
							raw->clear_values();
					}
				}

				RebindAlloc<char> al(*this);

				// Free all decompression contexts
				for (auto it = d_contexts.begin(); it != d_contexts.end();) {
					auto next = it;
					++next;
					RawType* r = (RawType*)*it;
					if (r->buffer)
						bool stop = true; // TEST
					al.deallocate(reinterpret_cast<char*>(r), sizeof(RawType));
					it = next;
				}

				// Reset all
				d_contexts.clear();
				d_buckets.clear();
				d_size = 0;
			}

			/// @brief Returns a new raw buffer, might throw
			auto make_raw() -> RawType*
			{
				RebindAlloc<char> al(*this);
				return make_raw_buffer<T, block_size>(al);
			}

			/// @brief Returns the back bucket size
			STENOS_ALWAYS_INLINE auto back_size() const noexcept -> size_t
			{
				if (d_buckets.size()) {
					if (d_buckets.back().buffer)
						return static_cast<size_t>(elems_per_block);
					return static_cast<size_t>(d_buckets.back().load_decompressed()->size);
				}
				return 0;
			}

			/// @brief Returns the front bucket size
			STENOS_ALWAYS_INLINE auto front_size() const noexcept -> size_t
			{
				if (d_buckets.size()) {
					if (d_buckets.front().buffer)
						return static_cast<size_t>(elems_per_block);
					return static_cast<size_t>(d_buckets.size() > 1 ? block_size : d_buckets.front().load_decompressed()->size);
				}
				return 0;
			}

			/// @brief Returns the container size
			STENOS_ALWAYS_INLINE auto size() const noexcept -> size_t { return d_size; }

			/// @brief Returns the compression ratio achieved by the block encoder
			auto compression_ratio() const noexcept -> float
			{
				size_t decompressed_size = d_buckets.size();
				if (d_buckets.size() && d_buckets.back().csize == 0)
					--decompressed_size;
				decompressed_size *= block_size * sizeof(T);
				size_t compress_size = 0;
				for (size_t i = 0; i < d_buckets.size(); ++i)
					if (d_buckets[i].buffer)
						compress_size += d_buckets[i].csize;
				return (compress_size && decompressed_size) ? decompressed_size / static_cast<float>(compress_size) : 0.f;
			}

			/// @brief Returns the current compression ratio, that is the total memory footprint of this container divided by its thoric size (size()*sizeof(T))
			auto current_compression_ratio() const noexcept -> float { return static_cast<float>(d_size * sizeof(T)) / static_cast<float>(memory_footprint()); }

			/// @brief Compress dirty blocks and release all unnecessary decompssion contexts
			void shrink_to_fit()
			{
				ContextType new_contexts;

				const size_t max_buffers = 1;

				for (auto it = d_contexts.begin(); it != d_contexts.end();) {
					RawType* raw = *it++;

					// Try to reuse the decompression context
					if (raw->size > 0 && raw->size < elems_per_block) {
						// Mandatory: reuse a non full decompression context (back context)
						new_contexts.push_back(raw);
						continue;
					}
					else if (new_contexts.size() < max_buffers && !raw->dirty) {
						// Reuse a non dirty decompression context
						new_contexts.push_back(raw);
						if (raw->block_index != RawType::invalid_index)
							d_buckets[raw->block_index].data.set(raw->buffer, Compressed);
						raw->reset();
						continue;
					}

					// If the context is dirty, compress it
					if (raw->dirty) {
						// Find the corresponding PackBuffer, excluding front and back buckets
						size_t index = raw->block_index;
						STENOS_ASSERT_DEBUG(index != RawType::invalid_index, "raw block must belong to an existing bucket");
						// Compress
						size_t r = compress(raw->storage);
						if (r != d_buckets[index].csize) {
							// Free old buffer, alloc new one, update compressed size, might throw (fine)
							char* buff = this->allocate_buffer_for_compression((unsigned)r, &d_buckets[index], index, raw);

							if (d_buckets[index].buffer)
								RebindAlloc<char>(*this).deallocate(d_buckets[index].buffer, d_buckets[index].csize);
							d_buckets[index].csize = (unsigned)r;
							d_buckets[index].buffer = buff;
						}
						memcpy(d_buckets[index].buffer, compression_buffer(), r);
					}

					// Unlink this decompression context with its compressed buffer
					if (raw->block_index != RawType::invalid_index)
						d_buckets[raw->block_index].data.set(raw->buffer, Compressed);

					if (new_contexts.size() < max_buffers) {
						// Add this decompression  context to the new ones
						raw->reset();
						new_contexts.push_back(raw);
					}
					else {
						// Free decompression context
						RebindAlloc<char> al(*this);
						al.deallocate(reinterpret_cast<char*>(raw), sizeof(RawType));
					}
				}

				// Swap contexts
				d_contexts.assign(std::move(new_contexts));

				while (d_contexts.size() > 1) {
					for (auto it = d_contexts.begin(); it != d_contexts.end(); ++it) {
						if ((*it)->size == 0) {
							erase_context(*it);
							break;
						}
					}
				}

				// Shrink bucket vector
				d_buckets.shrink_to_fit();
			}

			/// @brief Try to lock a compression context
			STENOS_ALWAYS_INLINE bool try_lock(RawType* raw) noexcept
			{
				if (raw->block_index != RawType::invalid_index)
					return d_buckets[raw->block_index].ref_count.try_lock();
				return true;
			}
			/// @brief Unlock a decompression context
			STENOS_ALWAYS_INLINE void unlock(RawType* raw) noexcept
			{
				if (raw->block_index != RawType::invalid_index)
					return d_buckets[raw->block_index].ref_count.unlock();
			}

			/// @brief Allocate and return buffer of given size.
			/// In case of exception, decompress and destroy values, deallocate previous buffer, remove bucket, remove decompression context and rethrow.
			auto allocate_buffer_for_compression(unsigned size, BucketType* bucket, size_t bucket_index, RawType* context) -> char*
			{
				char* buff = nullptr;
				try {
					buff = RebindAlloc<char>(*this).allocate(size); //(char*)malloc(r);
				}
				catch (...) {
					// unlock bucket
					unlock(context);
					// deallocate
					if (bucket->data) {
						// first destroy values
						char* buffer = bucket->data.find_compressed();
						if (!std::is_trivially_destructible<T>::value) {
							context->size = block_size;
							this->decompress(bucket, context->storage);
							context->clear_values();
						}
						RebindAlloc<char>(*this).deallocate(buffer, bucket->csize);
					}
					// remove context
					erase_context(context);
					// remove bucket
					d_buckets.erase(d_buckets.begin() + static_cast<difference_type>(bucket_index));

					// update indexes
					for (size_t i = bucket_index; i < d_buckets.size(); ++i)
						if (auto raw = d_buckets[i].load_decompressed())
							raw->block_index = i;

					throw;
				}
				return buff;
			}

			/// @brief Returns a decompression context either by creating a new one, or by reusing an existing one
			auto make_or_find_free_context(RawType* exclude = nullptr) -> RawType*
			{
				if (d_contexts.size() >= 2)
					return find_free_context(exclude, nullptr);

				// Create a new context, might throw (fine)
				RawType* raw = make_raw();
				d_contexts.push_front(raw);
				return raw;
			}

			auto get_locked_context(RawType* exclude = nullptr, typename ContextType::iterator* start = nullptr) noexcept -> typename ContextType::iterator
			{
				// Start by the tail
				auto found = start ? *start : d_contexts.end();
				if (d_contexts.size()) {
					--found;
					if (found != d_contexts.end()) {
						while ((((*found)->size && (*found)->size != elems_per_block) || *found == exclude || !try_lock((*found)))) {
							if (found == d_contexts.begin()) {
								found = d_contexts.end();
								break;
							}
							--found;
						}
					}
				}
				return found;
			}

			/// @brief Reuse and return an existing decompression context that cannot be exclude one
			auto find_free_context(RawType* exclude = nullptr, typename ContextType::iterator* start = nullptr) -> RawType*
			{
				// All contexts used, compress one of them, if possible an empty or not dirty one

				// Start by the tail
				auto found = get_locked_context(exclude, start);

				if (found == d_contexts.end()) {
					if (start)
						return nullptr;
					// Cannot find one: create a new one, might throw (fine)
					RawType* raw = make_raw();

					// Insert the new context at the beginning
					d_contexts.push_front(raw);
					// Return it
					return raw;
				}

				RawType* found_raw = *found;
				BucketType* found_bucket = (*found)->block_index == RawType::invalid_index ? nullptr : &d_buckets[(*found)->block_index];
				size_t saved_index = (*found)->block_index;

				// Compress context if dirty
				if (found_raw->dirty) {
					// find the corresponding PackBuffer, excluding front and back buckets
					STENOS_ASSERT_DEBUG(found_bucket, "context must belong to an existing bucket");

					size_t r = compress(found_raw->storage);

					if (r != found_bucket->csize) {
						// Free old memory, alloc new one
						char* buff = allocate_buffer_for_compression((unsigned)r, found_bucket, saved_index, found_raw);
						if (found_raw->buffer)
							RebindAlloc<char>(*this).deallocate(found_raw->buffer, found_bucket->csize);
						found_bucket->csize = (unsigned)r;
						found_raw->buffer = buff;
					}

					memcpy(found_raw->buffer, compression_buffer(), r);

					// Use this opportunity to free another context if possible
					if (!start && d_contexts.size() > d_buckets.size() / 16) {
						RawType* raw = find_free_context(exclude, &found);
						if (raw)
							erase_context(raw);
					}
				}

				if (d_contexts.size() > 1 && found != d_contexts.begin()) {
					// Move the context to index 0.
					// This way, we maximize the chances to find at the tail the (possibly) oldest context that should be the first to be reused
					d_contexts.erase(found_raw);

					// Now that the found context is removed from the context list,
					// use this occasion to keep looking for an additional decompression context to close
					/* if (!start) {
						auto it = d_contexts.end();
						auto * to_free = find_free_context(exclude, &it);
						if (to_free) {
							erase_context(to_free);
						}
					}*/

					d_contexts.push_front(found_raw);
				}

				// Unlink
				if (found_bucket)
					found_bucket->data.set(found_raw->buffer, Compressed);

				// Reset, unlock and return
				unlock(found_raw);
				found_raw->reset();
				return found_raw;
			}

			/// @brief Compress bucket using its own context
			auto compress_bucket(size_t index) -> RawType*
			{
				BucketType* bucket = &d_buckets[index];
				RawType* decompressed = bucket->load_decompressed();

				// Compress
				size_t r = compress(decompressed->storage);
				if (r != bucket->csize) {
					char* buff = allocate_buffer_for_compression((unsigned)r, bucket, index, decompressed);
					if (decompressed->buffer)
						RebindAlloc<char>(*this).deallocate(decompressed->buffer, bucket->csize);
					decompressed->buffer = buff;
				}
				memcpy(decompressed->buffer, compression_buffer(), r);

				bucket->data.set(decompressed->buffer, Compressed);
				// free buckets
				bucket->csize = (unsigned)r;
				decompressed->reset();
				return decompressed;
			}

			/// @brief Ensure that a back bucket is available for back insertion
			void ensure_has_back_bucket()
			{
				if (d_buckets.empty() || d_buckets.back().data.compressed()) {
					// no buckets or the back buffer is compressed, create a new one
					RawType* raw = make_or_find_free_context();
					raw->mark_dirty();
					// might throw, fine as we did not specify yet the block index
					d_buckets.push_back(BucketType(raw));
					raw->block_index = d_buckets.size() - 1;
				}
				else if (d_buckets.back().load_decompressed()->size == elems_per_block) {
					RawType* raw = compress_bucket(d_buckets.size() - 1);
					raw->mark_dirty();
					// might throw, fine as we did not specify yet the block index
					d_buckets.push_back(BucketType(raw));
					raw->block_index = d_buckets.size() - 1;
				}
			}

			/// @brief Decompress given bucket.
			/// If necessary, use an existing context or create a new one (which cannot be the exclude one)
			auto decompress_bucket(size_t index, size_t exclude = static_cast<size_t>(-1)) -> RawType*
			{
				auto decompressed = d_buckets[index].load_decompressed();
				if (!decompressed) {
					std::lock_guard<SharedSpinner> lock(d_lock);
					if ((decompressed = d_buckets[index].load_decompressed()))
						return decompressed;

					BucketType* pack = &d_buckets[index];
					RawType* raw = make_or_find_free_context(exclude == static_cast<size_t>(-1) ? nullptr : d_buckets[exclude].load_decompressed());
					raw->block_index = index;

					this->decompress(pack, raw->storage);
					char* buffer = pack->data.find_compressed();
					pack->data.set(raw, Raw);
					raw->buffer = buffer;
					raw->dirty = 0;
					raw->size = block_size;
					decompressed = raw;
				}
				return decompressed;
			}

			/// @brief Returns the class total memory footprint, including sizeof(*this)
			auto memory_footprint() const noexcept -> size_t
			{
				size_t res = stenos_memory_footprint(d_ctx);
				for (size_t i = 0; i < d_buckets.size(); ++i)
					res += d_buckets[i].csize;
				res += d_buckets.capacity() * sizeof(BucketType);
				res += d_contexts.size() * sizeof(RawType);
				res += sizeof(*this);
				return res;
			}

			/// @brief Back insertion
			template<class... Args>
			STENOS_ALWAYS_INLINE void emplace_back(Args&&... args)
			{
				// All functions might throw, fine (strong guarantee)
				if (!(d_buckets.size() && !d_buckets.back().data.find_compressed() && d_buckets.back().load_decompressed()->size < elems_per_block))
					ensure_has_back_bucket();

				BucketType& bucket = d_buckets.back();
				try {
					// might throw, see below
					auto raw = bucket.load_decompressed();
					new (&raw->at(raw->size)) T(std::forward<Args>(args)...);

					++raw->size;
					d_size++;
				}
				catch (...) {
					// Exception: if we just created the back bucket, we must remove it
					if (d_buckets.back().load_decompressed()->size == 0) {
						d_buckets.back().load_decompressed()->block_index = RawType::invalid_index;
						d_buckets.back().load_decompressed()->mark_not_dirty(); // mark not dirty anymore
						d_buckets.pop_back();
					}
					throw;
				}
			}
			/// @brief Back insertion
			STENOS_ALWAYS_INLINE void push_back(const T& value) { emplace_back(value); }
			/// @brief Back insertion
			STENOS_ALWAYS_INLINE void push_back(T&& value) { emplace_back(std::move(value)); }

			/// @brief Remove context from list of contexts and deallocate it.
			/// Do not forget to destroy its content first!!!
			void erase_context(RawType* r)
			{
				d_contexts.erase(r);
				RebindAlloc<char> al(*this);
				al.deallocate(reinterpret_cast<char*>(r), sizeof(RawType));
			}
			void deallocate_buffer(size_t index) noexcept
			{
				if (auto buf = d_buckets[index].data.find_compressed()) {
					RebindAlloc<char>(*this).deallocate(buf, d_buckets[index].csize);
					if (d_buckets[index].data.compressed())
						d_buckets[index].data.set(nullptr, Compressed);
					else
						d_buckets[index].data.raw()->buffer = nullptr;
					d_buckets[index].csize = 0;
				}
			}

			/// @brief Remove back value
			void pop_back()
			{
				STENOS_ASSERT_DEBUG(size() > 0, "calling pop_back on empty container");
				auto raw = d_buckets.back().load_decompressed();
				// remove empty back bucket
				if (raw && raw->size == 0) {
					deallocate_buffer(d_buckets.size() - 1);
					// erase_context(raw);
					raw->reset(); // TEST

					// Ensure we can lock the bucket to avoid dangling references
					check_destroy_bucket(d_buckets.size() - 1);

					d_buckets.pop_back();
					raw = d_buckets.back().load_decompressed();
				}
				// decompress back bucket if necessary
				if (!raw)
					raw = decompress_bucket(d_buckets.size() - 1);

				// destroy element
				if (!std::is_trivially_destructible<T>::value)
					destroy_ptr(&raw->at(raw->size - 1));

				// Here we can mark the buffer as dirty
				// without holding the lock on the bucket
				// as emplace_back() is not supposed to
				// work in multi-threaded context.
				raw->mark_dirty(this); // destroy compressed buffer if necessary
				--raw->size;
				--d_size;

				// destroy back bucket if necessary, as well as decompression context
				if (raw->size == 0) {
					deallocate_buffer(d_buckets.size() - 1);
					// erase_context(raw);
					raw->reset(); // TEST

					// Ensure we can lock the bucket to avoid dangling references
					check_destroy_bucket(d_buckets.size() - 1);

					d_buckets.pop_back();
				}
			}

			/// @brief Reserve is a no-op
			void reserve(size_t) noexcept
			{
				// No-op
			}

			/// @brief Resize to lower size
			void resize_shrink(size_t new_size)
			{
				// Pop back until we reach a size multiple of block_size
				while (size() > new_size && (size() & (block_size - 1)))
					pop_back();

				if (size() > block_size) {

					// Remove full blocks
					while (size() > new_size + block_size) {

						// Ensure we can lock the bucket to avoid dangling references
						check_destroy_bucket(d_buckets.size() - 1);

						// destroy values in last bucket
						if (!std::is_trivially_destructible<T>::value) {
							if (!d_buckets.back().load_decompressed())
								decompress_bucket(d_buckets.size() - 1);
							d_buckets.back().load_decompressed()->clear_values();
						}

						deallocate_buffer(d_buckets.size() - 1);
						if (d_buckets.back().load_decompressed())
							erase_context(d_buckets.back().load_decompressed());
						d_buckets.pop_back();

						d_size -= block_size;
					}
				}
				// Pop back remaining values
				while (size() > new_size)
					pop_back();
			}

			void resize(size_t new_size)
			{
				if (new_size == 0)
					clear();
				else if (new_size == size())
					return;
				else if (new_size > size()) {

					// finish filling back buffer
					while (size() < new_size && (size() & (block_size - 1)))
						emplace_back();

					// fill by chunks of block_size elements
					if (new_size > block_size) {

						// temporary storage for chunks of block_size elements
						RawType raw;
						raw.size = 0;

						while (size() < new_size - block_size) {
							size_t r = 0;
							// Generic way to compress one value repeated
							raw.size = block_size;
							// construct, might throw, fine
							for (unsigned i = 0; i < block_size; ++i)
								construct_ptr(&raw.at(i));
							// compress
							r = compress(raw.storage);

							char* buff = nullptr;
							try {
								// might throw, see below
								buff = RebindAlloc<char>(*this).allocate(r);
								d_buckets.push_back(BucketType(nullptr, buff, (unsigned)r));
								memcpy(buff, compression_buffer(), r);
							}
							catch (...) {
								// In case of exception, free buffer if necessary and destroy elements
								if (buff)
									RebindAlloc<char>(*this).deallocate(buff, r);
								raw.clear_values();
								throw;
							}
							d_size += block_size;
						}
					}

					// finish with last elements
					while (size() < new_size)
						emplace_back();
				}
				else {
					resize_shrink(new_size);
				}
			}

			void resize(size_t new_size, const T& val)
			{
				if (new_size == 0)
					clear();
				else if (new_size == size())
					return;
				else if (new_size > size()) {
					// finish filling back buffer
					while (size() < new_size && (size() & (block_size - 1)))
						emplace_back(val);

					// fill by chunks of block_size elements
					if (new_size > block_size) {

						// temporary storage for chunks of block_size elements
						RawType raw;
						raw.size = 0;

						while (size() < new_size - block_size) {
							size_t r = 0;
							raw.size = block_size;
							// construct, might throw, fine
							for (unsigned i = 0; i < block_size; ++i)
								construct_ptr(&raw.at(i), val);
							// compress
							r = compress(raw.storage);

							char* buff = nullptr;
							try {
								buff = RebindAlloc<char>(*this).allocate(r);
								d_buckets.push_back(BucketType(nullptr, buff, (unsigned)r));
								memcpy(buff, compression_buffer(), r);
							}
							catch (...) {
								// In case of exception, free buffer if necessary and destroy elements
								if (buff)
									RebindAlloc<char>(*this).deallocate(buff, r);
								raw.clear_values();
								throw;
							}
							d_size += block_size;
						}
					}

					// finish with last elements
					while (size() < new_size)
						emplace_back(val);
				}
				else {
					resize_shrink(new_size);
				}
			}

			/// @brief Erase range
			auto erase(const_iterator first, const_iterator last) -> const_iterator
			{
				STENOS_ASSERT_DEBUG(last >= first && first >= const_iterator(this, 0) && last <= const_iterator(this, size()), "cvector erase iterator outside range");
				if (first == last)
					return last;

				difference_type off = first - const_iterator(this, 0);
				size_t count = static_cast<size_t>(last - first);

				auto it = iterator(first);
				std::move(iterator(last), iterator(this, size()), iterator(first));
				if (count == 1)
					pop_back();
				else
					resize(size() - count);

				return const_iterator(this, 0) + off;
			}

			/// @brief Erase middle
			auto erase(const_iterator pos) -> const_iterator { return erase(pos, pos + 1); }

			/// @brief Insert middle
			template<class... Args>
			auto emplace(const_iterator pos, Args&&... args) -> iterator
			{
				difference_type dist = pos - const_iterator(this, 0);
				STENOS_ASSERT_DEBUG(static_cast<size_t>(dist) <= size(), "cvector: invalid insertion location");

				// insert on the right side
				this->emplace_back(std::forward<Args>(args)...);
				std::rotate(iterator(this, 0) + dist, iterator(this, size()) - 1, iterator(this, size()));

				return iterator(this, 0) + dist;
			}

			/// @brief Insert range
			template<class InputIt>
			auto insert(const_iterator pos, InputIt first, InputIt last) -> iterator
			{
				// Check back insertion
				if ((size_t)pos.abspos == size()) {
					for (; first != last; ++first)
						push_back(*first);
					return iterator(this, pos.abspos);
				}

				difference_type off = (pos - const_iterator(this, 0));
				size_t oldsize = size();

				STENOS_ASSERT_DEBUG(static_cast<size_t>(off) <= size(), "cvector insert iterator outside range");

				if (size_t len = stenos::distance(first, last)) {
					// For random access iterators

					try {
						resize(size() + len);

						std::move_backward(iterator(this, 0) + off, iterator(this, 0) + static_cast<difference_type>(size() - len), iterator(this, size()));

						// std::copy(first, last, iterator(this, 0) + off);
						// use for_each instead of std::copy
						for_each(static_cast<size_t>(off), static_cast<size_t>(off) + len, [&first](T& v) {
							v = *first;
							++first;
						});
					}
					catch (...) {
						for (; oldsize < size();)
							pop_back(); // restore old size, at least
						throw;
					}
					return (iterator(this, 0) + off);
				}

				// Non random access iterators
				if (first == last)
					;
				else {

					try {
						for (; first != last; ++first)
							push_back(*first); // append
					}
					catch (...) {
						for (; oldsize < size();)
							pop_back(); // restore old size, at least
						throw;
					}

					std::rotate(iterator(this, 0) + off, iterator(this, 0) + static_cast<difference_type>(oldsize), iterator(this, size()));
				}
				return (iterator(this, 0) + off);
			}

			STENOS_ALWAYS_INLINE auto at(size_t pos) noexcept -> ref_type { return ref_type(this, pos >> shift, pos & mask); }
			STENOS_ALWAYS_INLINE auto at(size_t pos) const noexcept -> const_ref_type { return const_ref_type(this, pos >> shift, pos & mask); }
			STENOS_ALWAYS_INLINE auto cat(size_t pos) const noexcept -> const_ref_type { return const_ref_type(this, pos >> shift, pos & mask); }
			STENOS_ALWAYS_INLINE auto operator[](size_t pos) noexcept -> ref_type { return at(pos); }
			STENOS_ALWAYS_INLINE auto operator[](size_t pos) const noexcept -> const_ref_type { return at(pos); }
			STENOS_ALWAYS_INLINE auto back() noexcept -> ref_type { return at(size() - 1); }
			STENOS_ALWAYS_INLINE auto back() const noexcept -> const_ref_type { return at(size() - 1); }
			STENOS_ALWAYS_INLINE auto front() noexcept -> ref_type { return at(0); }
			STENOS_ALWAYS_INLINE auto front() const noexcept -> const_ref_type { return at(0); }
			STENOS_ALWAYS_INLINE auto max_size() const noexcept -> size_t { return std::numeric_limits<difference_type>::max(); }

			template<class Functor>
			auto const_for_each(size_t start, size_t end, Functor&& fun) const -> size_t
			{
				STENOS_ASSERT_DEBUG(start <= end, "const_for_each: invalid range");
				STENOS_ASSERT_DEBUG(end <= d_size, "const_for_each: invalid range");
				if (start == end)
					return 0;

				size_t remaining = end - start;
				size_t bindex = start >> shift;
				size_t pos = start & mask;
				size_t res = 0;

				while (remaining) {
					size_t to_process = std::min(remaining, static_cast<size_t>(block_size - pos));
					std::shared_lock<SharedSpinner> lock(d_buckets[bindex].get().ref_count);
					const RawType* cur = d_buckets[bindex].load_decompressed();
					if (!cur) {
						cur = const_cast<ThisType*>(this)->decompress_bucket(bindex);
					}
					remaining -= to_process;
					size_t en = pos + to_process;
					for (size_t p = pos; p != en; ++p, ++res)
						if (!eval_functor(std::forward<Functor>(fun), cur->at(p)))
							return res;
					pos = 0;
					++bindex;
				}
				return res;
			}

			template<class Functor>
			auto for_each(size_t start, size_t end, Functor&& fun) -> size_t
			{
				STENOS_ASSERT_DEBUG(start <= end, "for_each: invalid range");
				STENOS_ASSERT_DEBUG(end <= d_size, "for_each: invalid range");
				if (start == end)
					return 0;

				size_t remaining = end - start;
				size_t bindex = start >> shift;
				size_t pos = start & mask;
				size_t res = 0;

				while (remaining) {
					size_t to_process = std::min(remaining, static_cast<size_t>(block_size - pos));
					std::lock_guard<SharedSpinner> lock(d_buckets[bindex].ref_count);
					RawType* cur = d_buckets[bindex].load_decompressed();
					if (!cur) {
						cur = this->decompress_bucket(bindex);
					}
					remaining -= to_process;
					size_t en = pos + to_process;
					for (size_t p = pos; p != en; ++p, ++res)
						if (!eval_functor(std::forward<Functor>(fun), cur->at(p)))
							return res;
					pos = 0;
					++bindex;
					cur->mark_dirty(this);
				}
				return res;
			}

			template<class Functor>
			auto const_for_each_backward(size_t first, size_t last, Functor&& fun) const -> size_t
			{
				STENOS_ASSERT_DEBUG(first <= last, "for_each_backward: invalid range");
				STENOS_ASSERT_DEBUG(last <= d_size, "for_each_backward: invalid range");
				if (first == last)
					return 0;

				--last;
				difference_type last_bucket = static_cast<difference_type>(last >> shift);
				difference_type last_index = static_cast<difference_type>(last & mask);
				difference_type first_bucket = static_cast<difference_type>(first >> shift);
				difference_type first_index = static_cast<difference_type>(first & mask);
				size_t res = 0;

				for (difference_type bindex = last_bucket; bindex >= first_bucket; --bindex) {
					std::shared_lock<SharedSpinner> lock(d_buckets[bindex].ref_count);
					const RawType* cur = d_buckets[bindex].load_decompressed();
					if (!cur) {
						cur = const_cast<ThisType*>(this)->decompress_bucket(bindex);
					}
					difference_type low = bindex == first_bucket ? first_index : 0;
					difference_type high = bindex == last_bucket ? last_index : static_cast<difference_type>(block_size - 1);
					for (difference_type i = high; i >= low; --i, ++res)
						if (!eval_functor(std::forward<Functor>(fun), cur->at((unsigned)i)))
							return res;
				}

				return res;
			}

			template<class Functor>
			auto for_each_backward(size_t first, size_t last, Functor&& fun) -> size_t
			{
				STENOS_ASSERT_DEBUG(first <= last, "for_each_backward: invalid range");
				STENOS_ASSERT_DEBUG(last <= d_size, "for_each_backward: invalid range");
				if (first == last)
					return 0;

				--last;
				difference_type last_bucket = static_cast<difference_type>(last >> shift);
				difference_type last_index = static_cast<difference_type>(last & mask);
				difference_type first_bucket = static_cast<difference_type>(first >> shift);
				difference_type first_index = static_cast<difference_type>(first & mask);
				size_t res = 0;

				for (difference_type bindex = last_bucket; bindex >= first_bucket; --bindex) {
					std::lock_guard<SharedSpinner> lock(d_buckets[bindex].ref_count);
					RawType* cur = d_buckets[bindex].load_decompressed();
					if (!cur) {
						cur = this->decompress_bucket(bindex);
					}
					difference_type low = bindex == first_bucket ? first_index : 0;
					difference_type high = bindex == last_bucket ? last_index : static_cast<difference_type>(block_size - 1);
					for (difference_type i = high; i >= low; --i, ++res)
						if (!eval_functor(std::forward<Functor>(fun), cur->at((unsigned)i)))
							return res;
					cur->mark_dirty(this);
				}

				return res;
			}
		};

		// Check for input iterator
		template<typename Iterator>
		using IfIsInputIterator = typename std::enable_if<std::is_convertible<typename std::iterator_traits<Iterator>::iterator_category, std::input_iterator_tag>::value, bool>::type;
	}

	/// @brief vector like class using compression to store its elements
	/// @tparam T value type, must be relocatable
	/// @tparam Allocator allocator type
	/// @tparam Level acceleratio nparameter for the compression algorithm, from 0 to 7
	/// @tparam Encoder encoder type, default to DefaultEncoder
	/// @tparam block_size number of elements per chunks, must be a power of 2
	///
	/// stenos::cvector is a is a random-access container with an interface similar to std::vector but storing its element in a compressed way.
	/// Its goal is to reduce the memory footprint of the container while providing performances as close as possible to std::vector.
	///
	template<class T, unsigned BlockSize = 0, int Level = 1, class Allocator = std::allocator<T>>
	class cvector : private Allocator
	{
		static_assert(Level <= 9, "invalid compression level");
		static_assert(sizeof(T) < STENOS_MAX_BYTESOFTYPE, "invalide type size");
		static_assert(((sizeof(T) * 256) << BlockSize) < STENOS_MAX_BLOCK_BYTES, "invalid block size");

		using internal_type = detail::CompressedVectorInternal<T, Allocator, BlockSize, Level>;
		using bucket_type = typename internal_type::BucketType;
		template<class U>
		using RebindAlloc = typename std::allocator_traits<Allocator>::template rebind_alloc<U>;

		static constexpr size_t shift = internal_type::shift;

		internal_type* d_data; // Store a pointer to internal data, easier to provide noexcept move constructor/copy and iterator stability on swap

		internal_type* make_internal(const Allocator& al)
		{
			RebindAlloc<internal_type> a = al;
			internal_type* ret = nullptr;
			try {
				ret = a.allocate(1);
				return new (ret) internal_type(al);
			}
			catch (...) {
				if (ret)
					a.deallocate(ret, 1);
				throw;
			}
			return ret;
		}
		void destroy_internal(internal_type* data) noexcept
		{
			data->~internal_type();
			RebindAlloc<internal_type> a = get_allocator();
			a.deallocate(data, 1);
		}

		void make_data_if_null()
		{
			if (!d_data)
				d_data = make_internal(get_allocator());
		}

		/// @brief Returns the compressed buffer for given block.
		/// This function will compress if necessary the corresponding block and deallocate the decompression context (if any).
		/// For all blocks except the last one, this function returns a view on compression buffer.
		/// For the last block, returns a null buffer.
		std::pair<const char*, size_t> compressed_block(size_t pos)
		{
			if (!d_data || pos == d_data->d_buckets.size() - 1)
				return { nullptr, 0 };

			auto* bucket = &d_data->d_buckets[pos];

			if (!bucket->buffer || (bucket->load_decompressed() && bucket->load_decompressed()->dirty)) {
				// compress bucket
				auto* raw = d_data->compress_bucket(pos);

				// erase decompression context if this is not the last one
				d_data->erase_context(raw);
			}

			return { bucket->buffer, bucket->csize };
		}

	public:
		static_assert(is_relocatable<T>::value, "cvector: given type must be relocatable based on stenos::is_relocatable type trait");

		using value_type = T;
		using reference = T&;
		using const_reference = const T&;
		using pointer = T*;
		using const_pointer = const T*;
		using size_type = size_t;
		using difference_type = typename std::allocator_traits<Allocator>::difference_type;
		using allocator_type = Allocator;
		using iterator = typename internal_type::iterator;
		using const_iterator = typename internal_type::const_iterator;
		using reverse_iterator = std::reverse_iterator<iterator>;
		using const_reverse_iterator = std::reverse_iterator<const_iterator>;
		using lock_type = detail::SharedSpinner;

		// Value type returned by at() and operator[]
		using ref_type = typename internal_type::ref_type;
		using const_ref_type = typename internal_type::const_ref_type;

		static constexpr size_t block_size = internal_type::block_size;
		static constexpr size_t block_bytes = internal_type::block_bytes;

		/// @brief Default constructor, initialize the internal bucket manager.
		cvector() noexcept(noexcept(Allocator()))
		  : Allocator()
		  , d_data(nullptr)
		{
		}
		/// @brief Constructs an empty container with the given allocator alloc.
		/// @param alloc allocator object
		explicit cvector(const Allocator& alloc)
		  : Allocator(alloc)
		  , d_data(make_internal(alloc))
		{
		}
		/// @brief Constructs the container with \a count copies of elements with value \a value.
		/// @param count new cvector size
		/// @param value the value to initialize elements of the container with
		/// @param alloc allocator object
		cvector(size_type count, const T& value, const Allocator& alloc = Allocator())
		  : Allocator(alloc)
		  , d_data(make_internal(alloc))
		{
			resize(count, value);
		}
		/// @brief Constructs the container with count default-inserted instances of T. No copies are made.
		/// @param count new cvector size
		/// @param alloc allocator object
		explicit cvector(size_type count, const Allocator& alloc = Allocator())
		  : Allocator(alloc)
		  , d_data(make_internal(alloc))
		{
			resize(count);
		}
		/// @brief Copy constructor. Constructs the container with the copy of the contents of other.
		/// @param other another container to be used as source to initialize the elements of the container with
		cvector(const cvector& other)
		  : cvector(other, copy_allocator(other.get_allocator()))
		{
		}
		/// @brief Constructs the container with the copy of the contents of other, using alloc as the allocator.
		/// @param other other another container to be used as source to initialize the elements of the container with
		/// @param alloc allcoator object
		cvector(const cvector& other, const Allocator& alloc)
		  : Allocator(alloc)
		  , d_data(nullptr)
		{
			if (other.size()) {
				d_data = make_internal(alloc);
				// calling push_back is faster than resize + copy
				other.for_each(0, other.size(), [this](const T& v) { this->push_back(v); });
			}
		}
		/// @brief Move constructor. Constructs the container with the contents of other using move semantics. Allocator is obtained by move-construction from the allocator belonging to other.
		/// @param other another container to be used as source to initialize the elements of the container with
		cvector(cvector&& other) noexcept(std::is_nothrow_move_constructible<Allocator>::value)
		  : Allocator(std::move(other.get_allocator()))
		  , d_data(other.d_data)
		{
			other.d_data = nullptr;
		}
		/// @brief  Allocator-extended move constructor. Using alloc as the allocator for the new container, moving the contents from other; if alloc != other.get_allocator(), this results in
		/// an element-wise move.
		/// @param other another container to be used as source to initialize the elements of the container with
		/// @param alloc allocator object
		cvector(cvector&& other, const Allocator& alloc)
		  : Allocator(alloc)
		  , d_data(make_internal(alloc))
		{
			if (alloc == other.get_allocator()) {
				std::swap(d_data, other.d_data);
			}
			else {
				other.for_each(0, other.size(), [this](T& v) { this->push_back(std::move(v)); });
			}
		}

		/// @brief  Constructs the container with the contents of the range [first, last).
		/// @tparam Iter iterator type
		/// @param first first iterator of the range
		/// @param last last iterator of the range
		/// @param alloc allocator object
		template<class Iter, detail::IfIsInputIterator<Iter> = true>
		cvector(Iter first, Iter last, const Allocator& alloc = Allocator())
		  : Allocator(alloc)
		  , d_data(make_internal(alloc))
		{
			assign(first, last);
		}

		/// @brief Constructs the container with the contents of the initializer list \a init.
		/// @param lst initializer list
		/// @param alloc allocator object
		cvector(const std::initializer_list<T>& lst, const Allocator& alloc = Allocator())
		  : cvector(lst.begin(), lst.end(), alloc)
		{
		}

		/// @brief  Destructor
		~cvector() noexcept
		{
			if (d_data)
				destroy_internal(d_data);
		}

		/// @brief Move assignment operator.
		/// @param other another container to use as data source
		/// @return reference to this
		auto operator=(cvector&& other) noexcept(noexcept(std::declval<cvector&>().swap(std::declval<cvector&>()))) -> cvector&
		{
			this->swap(other);
			return *this;
		}

		/// @brief Copy assignment operator.
		/// @param other another container to use as data source
		/// @return reference to this
		auto operator=(const cvector& other) -> cvector&
		{
			if (this != std::addressof(other)) {

				if STENOS_CONSTEXPR (assign_alloc<Allocator>::value) {
					if (get_allocator() != other.get_allocator()) {
						destroy_internal(d_data);
						d_data = nullptr;
					}
				}
				assign_allocator(get_allocator(), other.get_allocator());

				if (other.size() == 0)
					clear();
				else {
					internal_type* tmp = make_internal(get_allocator());
					try {
						try {
							other.for_each(0, other.size(), [tmp](const T& v) { tmp->push_back(v); });
						}
						catch (...) {
							destroy_internal(tmp);
							throw;
						}
						destroy_internal(d_data);
						d_data = tmp;
					}
					catch (...) {
						destroy_internal(tmp);
					}
				}
			}
			return *this;
		}

		/// @brief Returns the total memory footprint in bytes of this cvector, excluding sizeof(*this)
		auto memory_footprint() const noexcept -> size_t { return d_data ? d_data->memory_footprint() : 0; }

		/// @brief Returns the compression ratio achieved by the block encoder
		auto compression_ratio() const noexcept -> float { return d_data ? d_data->compression_ratio() : 0; }

		/// @brief Returns the current compression ratio, which is the total memory footprint of this container divided by its theoric size (size()*sizeof(T))
		auto current_compression_ratio() const noexcept -> float { return d_data ? d_data->current_compression_ratio() : 0; }

		/// @brief Returns the container size.
		STENOS_ALWAYS_INLINE auto size() const noexcept -> size_type { return d_data ? d_data->size() : 0; }
		/// @brief Returns the container maximum size.
		static auto max_size() noexcept -> size_type { return std::numeric_limits<difference_type>::max(); }
		/// @brief Retruns true if the container is empty, false otherwise.
		STENOS_ALWAYS_INLINE auto empty() const noexcept -> bool { return !d_data || d_data->size() == 0; }
		/// @brief Returns the allocator associated with the container.
		STENOS_ALWAYS_INLINE auto get_allocator() const noexcept -> const Allocator& { return static_cast<const Allocator&>(*this); }
		/// @brief Returns the allocator associated with the container.
		STENOS_ALWAYS_INLINE auto get_allocator() noexcept -> Allocator& { return static_cast<Allocator&>(*this); }
		/// @brief Exchanges the contents of the container with those of other. Does not invoke any move, copy, or swap operations on individual elements.
		/// @param other other sequence to swap with
		/// All iterators and references remain valid.
		/// An iterator holding the past-the-end value in this container will refer to the other container after the operation.
		void swap(cvector& other) noexcept(noexcept(swap_allocator(std::declval<Allocator&>(), std::declval<Allocator&>())))
		{
			if (this != std::addressof(other)) {
				std::swap(d_data, other.d_data);
				swap_allocator(get_allocator(), other.get_allocator());
			}
		}

		/// @brief Release all unused memory, and compress all dirty blocks.
		/// This function does NOT invalidate iterators, except if an exception is thrown.
		/// Basic exception guarantee only.
		void shrink_to_fit()
		{
			if (d_data)
				d_data->shrink_to_fit();
		}

		/// @brief Resizes the container to contain count elements.
		/// @param count new size of the container
		/// If the current size is greater than count, the container is reduced to its first count elements.
		/// If the current size is less than count, additional default-inserted elements are appended.
		/// Basic exception guarantee.
		void resize(size_type count)
		{
			if (count == this->size())
				return; // No-op
			else if (count == 0)
				clear();
			else {
				make_data_if_null();
				d_data->resize(count);
			}
		}

		/// @brief Resizes the container to contain count elements.
		/// @param count new size of the container
		/// @param value the value to initialize the new elements with
		/// If the current size is greater than count, the container is reduced to its first count elements.
		/// If the current size is less than count, additional copies of value are appended.
		/// Basic exception guarantee.
		void resize(size_type count, const T& value)
		{
			if (count == this->size())
				return; // No-op
			else if (count == 0)
				clear();
			else {
				make_data_if_null();
				d_data->resize(count, value);
			}
		}

		/// @brief Clear the container.
		void clear() noexcept
		{
			if (!empty())
				d_data->clear();
		}

		/// @brief Appends the given element value to the end of the container.
		/// @param value the value of the element to append
		/// Strong exception guarantee.
		STENOS_ALWAYS_INLINE void push_back(const T& value)
		{
			make_data_if_null();
			d_data->push_back(value);
		}

		/// @brief Appends the given element value to the end of the container using move semantic.
		/// @param value the value of the element to append
		/// Strong exception guarantee.
		STENOS_ALWAYS_INLINE void push_back(T&& value)
		{
			make_data_if_null();
			d_data->push_back(std::move(value));
		}

		/// @brief Appends a new element to the end of the container
		/// @return reference to inserted element
		/// Strong exception guarantee.
		template<class... Args>
		STENOS_ALWAYS_INLINE auto emplace_back(Args&&... args) -> ref_type
		{
			make_data_if_null();
			d_data->emplace_back(std::forward<Args>(args)...);
			return back();
		}

		/// @brief Insert \a value before \a it
		/// @param it iterator within the cvector
		/// @param value element to insert
		/// Basic exception guarantee.
		auto insert(const_iterator it, const T& value) -> iterator { return emplace(it, value); }

		/// @brief Insert \a value before \a it using move semantic.
		/// @param it iterator within the cvector
		/// @param value element to insert
		/// Basic exception guarantee.
		auto insert(const_iterator it, T&& value) -> iterator { return emplace(it, std::move(value)); }
		/// @brief Inserts a new element into the container directly before \a pos.
		/// @param pos iterator within the cvector
		/// @return reference to inserted element
		/// Basic exception guarantee.
		template<class... Args>
		auto emplace(const_iterator pos, Args&&... args) -> iterator
		{
			make_data_if_null();
			d_data->emplace(pos, std::forward<Args>(args)...);
			return begin() + pos.abspos;
		}

		/// @brief Inserts elements from range [first, last) before it.
		/// @tparam Iter iterator type
		/// @param it iterator within the cvector
		/// @param first first iterator of the range
		/// @param last last iterator of the range
		/// @return Iterator pointing to the first element inserted, or it if first==last
		/// Basic exception guarantee.
		template<class Iter, detail::IfIsInputIterator<Iter> = true>
		auto insert(const_iterator it, Iter first, Iter last) -> iterator
		{
			make_data_if_null();
			return d_data->insert(it, first, last);
		}

		/// @brief Inserts elements from initializer list ilist before pos.
		/// @return Iterator pointing to the first element inserted, or it if first==last.
		/// Basic exception guarantee.
		auto insert(const_iterator pos, std::initializer_list<T> ilist) -> iterator { return insert(pos, ilist.begin(), ilist.end()); }

		/// @brief Inserts count copies of the value before pos
		/// Basic exception guarantee.
		void insert(size_type pos, size_type count, const T& value) { insert(pos, cvalue_iterator<T>(0, value), cvalue_iterator<T>(count, value)); }

		/// @brief Inserts count copies of the value before pos
		/// @return Iterator pointing to the first element inserted, or it if count==0
		/// Basic exception guarantee.
		auto insert(const_iterator pos, size_type count, const T& value) -> iterator { return insert(pos, cvalue_iterator<T>(0, value), cvalue_iterator<T>(count, value)); }

		/// @brief Removes the last element of the container.
		/// Calling pop_back on an empty container results in undefined behavior.
		/// Strong exception guarantee.
		STENOS_ALWAYS_INLINE void pop_back()
		{
			STENOS_ASSERT_DEBUG(d_data, "empty cvector");
			d_data->pop_back();
		}

		/// @brief Erase element at given position.
		/// @param it iterator to the element to remove
		/// @return iterator following the last removed element
		/// Basic exception guarantee.
		auto erase(const_iterator it) -> iterator
		{
			STENOS_ASSERT_DEBUG(d_data, "empty cvector");
			return d_data->erase(it);
		}

		/// @brief Removes the elements in the range [first, last).
		/// @param first iterator to the first element to erase
		/// @param last iterator to the last (excluded) element to erase
		/// @return Iterator following the last removed element.
		/// Basic exception guarantee.
		auto erase(const_iterator first, const_iterator last) -> iterator
		{
			STENOS_ASSERT_DEBUG(d_data, "empty cvector");
			return d_data->erase(first, last);
		}

		/// @brief Replaces the contents with \a count copies of value \a value
		/// Basic exception guarantee.
		void assign(size_type count, const T& value) { assign(cvalue_iterator<T>(0, value), cvalue_iterator<T>(count, value)); }

		/// @brief Replaces the contents with copies of those in the range [first, last). The behavior is undefined if either argument is an iterator into *this.
		/// Basic exception guarantee.
		template<class Iter, detail::IfIsInputIterator<Iter> = true>
		void assign(Iter first, Iter last)
		{

			if (size_t len = stenos::distance(first, last)) {
				// For random access iterators
				resize(len);
				for_each(0, size(), [&first](auto& val) {
					val = *first;
					++first;
				});
			}
			else {
				// For forward iterators
				size_t count = 0;
				auto it = begin();
				for (; first != last && count != size(); ++first, ++it, ++count)
					*it = *first;
				while (first != last) {
					push_back(*first);
					++first;
					++count;
				}
				resize(count);
			}
		}

		/// @brief Replaces the contents with the elements from the initializer list ilist.
		/// Basic exception guarantee.
		void assign(std::initializer_list<T> ilist) { assign(ilist.begin(), ilist.end()); }

		/// @brief Returns a reference wrapper to the element at specified location pos, with bounds checking.
		STENOS_ALWAYS_INLINE auto at(size_type pos) const -> const_ref_type
		{
			// random access
			if (pos >= size())
				throw std::out_of_range("");
			return (d_data->at(pos));
		}
		/// @brief Returns a reference wrapper to the element at specified location pos, with bounds checking.
		STENOS_ALWAYS_INLINE auto at(size_type pos) -> ref_type
		{
			// random access
			if (pos >= size())
				throw std::out_of_range("");
			return (d_data->at(pos));
		}
		/// @brief Returns a reference wrapper to the element at specified location pos, without bounds checking.
		STENOS_ALWAYS_INLINE auto operator[](size_type pos) const noexcept -> const_ref_type
		{
			// random access
			STENOS_ASSERT_DEBUG(d_data, "empty cvector");
			return (d_data->at(pos));
		}
		/// @brief Returns a reference wrapper to the element at specified location pos, without bounds checking.
		STENOS_ALWAYS_INLINE auto operator[](size_type pos) noexcept -> ref_type
		{
			// random access
			STENOS_ASSERT_DEBUG(d_data, "empty cvector");
			return (d_data->at(pos));
		}

		/// @brief Returns a reference wrapper to the last element in the container.
		STENOS_ALWAYS_INLINE auto back() noexcept -> ref_type
		{
			STENOS_ASSERT_DEBUG(d_data, "empty cvector");
			return d_data->back();
		}
		/// @brief Returns a reference wrapper to the last element in the container.
		STENOS_ALWAYS_INLINE auto back() const noexcept -> const_ref_type
		{
			STENOS_ASSERT_DEBUG(d_data, "empty cvector");
			return d_data->back();
		}
		/// @brief Returns a reference wrapper to the first element in the container.
		STENOS_ALWAYS_INLINE auto front() noexcept -> ref_type
		{
			STENOS_ASSERT_DEBUG(d_data, "empty cvector");
			return d_data->front();
		}
		/// @brief Returns a reference wrapper to the first element in the container.
		STENOS_ALWAYS_INLINE auto front() const noexcept -> const_ref_type
		{
			STENOS_ASSERT_DEBUG(d_data, "empty cvector");
			return d_data->front();
		}

		/// @brief Returns an iterator to the first element of the cvector.
		STENOS_ALWAYS_INLINE auto begin() const noexcept -> const_iterator { return const_iterator(d_data, 0); }
		/// @brief Returns an iterator to the first element of the cvector.
		STENOS_ALWAYS_INLINE auto begin() noexcept -> iterator { return iterator(d_data, 0); }
		/// @brief Returns an iterator to the element following the last element of the cvector.
		STENOS_ALWAYS_INLINE auto end() const noexcept -> const_iterator { return const_iterator(d_data, d_data ? static_cast<difference_type>(d_data->d_size) : 0); }
		/// @brief Returns an iterator to the element following the last element of the cvector.
		STENOS_ALWAYS_INLINE auto end() noexcept -> iterator { return iterator(d_data, d_data ? static_cast<difference_type>(d_data->d_size) : 0); }
		/// @brief Returns a reverse iterator to the first element of the reversed list.
		STENOS_ALWAYS_INLINE auto rbegin() noexcept -> reverse_iterator { return reverse_iterator(end()); }
		/// @brief Returns a reverse iterator to the first element of the reversed list.
		STENOS_ALWAYS_INLINE auto rbegin() const noexcept -> const_reverse_iterator { return const_reverse_iterator(end()); }
		/// @brief Returns a reverse iterator to the element following the last element of the reversed list.
		STENOS_ALWAYS_INLINE auto rend() noexcept -> reverse_iterator { return reverse_iterator(begin()); }
		/// @brief Returns a reverse iterator to the element following the last element of the reversed list.
		STENOS_ALWAYS_INLINE auto rend() const noexcept -> const_reverse_iterator { return const_reverse_iterator(begin()); }
		/// @brief Returns an iterator to the first element of the cvector.
		STENOS_ALWAYS_INLINE auto cbegin() const noexcept -> const_iterator { return begin(); }
		/// @brief Returns an iterator to the element following the last element of the cvector.
		STENOS_ALWAYS_INLINE auto cend() const noexcept -> const_iterator { return end(); }
		/// @brief Returns a reverse iterator to the first element of the reversed list.
		STENOS_ALWAYS_INLINE auto crbegin() const noexcept -> const_reverse_iterator { return rbegin(); }
		/// @brief Returns a reverse iterator to the element following the last element of the reversed list.
		STENOS_ALWAYS_INLINE auto crend() const noexcept -> const_reverse_iterator { return rend(); }

		/// @brief Apply functor on values in the range [first,last).
		/// The function can stop early if provided functor returns false.
		/// @tparam Functor functor type
		/// @param first first index (included)
		/// @param last last index (excluded)
		/// @param fun functor to be applied
		/// @return the number of successfully processed values.
		template<class Functor>
		auto for_each(size_t first, size_t last, Functor&& fun) -> size_t
		{
			if (d_data)
				return d_data->for_each(first, last, std::forward<Functor>(fun));
			return 0;
		}

		/// @brief Apply functor on values in the range [first,last).
		/// The function can stop early if provided functor returns false.
		/// @tparam Functor functor type
		/// @param first first index (included)
		/// @param last last index (excluded)
		/// @param fun functor to be applied
		/// @return the number of successfully processed values.
		template<class Functor>
		auto for_each(size_t first, size_t last, Functor&& fun) const -> size_t
		{
			if (d_data)
				return d_data->const_for_each(first, last, std::forward<Functor>(fun));
			return 0;
		}

		/// @brief Apply functor on values in the range [first,last).
		/// The function can stop early if provided functor returns false.
		/// @tparam Functor functor type
		/// @param first first index (included)
		/// @param last last index (excluded)
		/// @param fun functor to be applied
		/// @return the number of successfully processed values.
		template<class Functor>
		auto const_for_each(size_t first, size_t last, Functor&& fun) const -> size_t
		{
			return for_each(first, last, std::forward<Functor>(fun));
		}

		/// @brief Apply functor on values in the range [first,last) in backward order (from last-1 to first).
		/// The function can stop early if provided functor returns false.
		/// @tparam Functor functor type
		/// @param first first index (included)
		/// @param last last index (excluded)
		/// @param fun functor to be applied
		/// @return the number of successfully processed values.
		template<class Functor>
		auto for_each_backward(size_t first, size_t last, Functor&& fun) -> size_t
		{
			if (d_data)
				return d_data->for_each_backward(first, last, std::forward<Functor>(fun));
			return 0;
		}

		/// @brief Apply functor on values in the range [first,last) in backward order (from last-1 to first).
		/// The function can stop early if provided functor returns false.
		/// @tparam Functor functor type
		/// @param first first index (included)
		/// @param last last index (excluded)
		/// @param fun functor to be applied
		/// @return the number of successfully processed values.
		template<class Functor>
		auto for_each_backward(size_t first, size_t last, Functor&& fun) const -> size_t
		{
			if (d_data)
				return d_data->const_for_each_backward(first, last, std::forward<Functor>(fun));
			return 0;
		}

		/// @brief Apply functor on values in the range [first,last) in backward order (from last-1 to first).
		/// The function can stop early if provided functor returns false.
		/// @tparam Functor functor type
		/// @param first first index (included)
		/// @param last last index (excluded)
		/// @param fun functor to be applied
		/// @return the number of successfully processed values.
		template<class Functor>
		auto const_for_each_backward(size_t first, size_t last, Functor&& fun) const -> size_t
		{
			return for_each_backward(first, last, std::forward<Functor>(fun));
		}

		///////////////////////////
		// Serialization/deserialization
		///////////////////////////

		/// @brief Serialize cvector content into a buffer
		size_t serialize(void* _dst, size_t dst_size)
		{
			size_t r = stenos_private_create_compression_header(size() * sizeof(T), block_bytes, _dst, dst_size);
			if STENOS_UNLIKELY (stenos_has_error(r))
				return r;

			uint8_t* dst = (uint8_t*)_dst;
			uint8_t* dst_end = dst + dst_size;
			dst += r;

			if (!d_data || size() == 0)
				return (size_t)(dst - (uint8_t*)_dst);

			// Write all compressed blocks
			for (size_t i = 0; i < d_data->d_buckets.size(); ++i) {
				// Last bucket
				if (i == d_data->d_buckets.size() - 1) {
					// check if last bucket is empty
					auto* buffer = &d_data->d_buckets.back();
					auto* raw = buffer->load_decompressed();
					if (!raw || raw->size == 0)
						goto end;

					// check if last bucket is full and compressed
					if (raw->size == block_size && buffer->buffer) {
						if STENOS_UNLIKELY (dst + buffer->csize > dst_end)
							return STENOS_ERROR_DST_OVERFLOW;

						memcpy(dst, buffer->buffer, buffer->csize);
						dst += buffer->csize;
						goto end;
					}

					// compress last bucket
					r = d_data->compress(raw->storage, raw->size * sizeof(T));
					if STENOS_UNLIKELY (stenos_has_error(r))
						return r;
					if STENOS_UNLIKELY (dst + r > dst_end)
						return STENOS_ERROR_DST_OVERFLOW;

					memcpy(dst, d_data->compression_buffer(), r);
					dst += r;
					goto end;
				}

				auto buf = compressed_block(i);
				if STENOS_UNLIKELY (!buf.first)
					return STENOS_ERROR_ALLOC;

				if STENOS_UNLIKELY (dst + buf.second > dst_end)
					return STENOS_ERROR_DST_OVERFLOW;

				memcpy(dst, buf.first, buf.second);
				dst += buf.second;
			}

		end:
			return dst - (uint8_t*)_dst;
		}

		/// @brief Serialize cvector content into a std::ostream object
		template<class Ostream>
		size_t serialize(Ostream& oss);

		size_t deserialize(const void* _src, size_t src_size)
		{
			clear();
			make_data_if_null();

			const uint8_t* src = (const uint8_t*)_src;
			const uint8_t* src_end = src + src_size;

			stenos_info info;
			// Read frame info
			size_t r = stenos_get_info(src, sizeof(T), src_size, &info);
			if STENOS_UNLIKELY (stenos_has_error(r))
				return r;

			src += r;

			// Invalid superblock size
			if STENOS_UNLIKELY (block_bytes != info.superblock_size)
				return STENOS_ERROR_INVALID_INPUT;

			// Empty vector
			if STENOS_UNLIKELY (info.decompressed_size == 0)
				return 0;

			// Check decompressed size validity
			if STENOS_UNLIKELY (info.decompressed_size % sizeof(T) != 0)
				return STENOS_ERROR_INVALID_INPUT;

			size_t s = info.decompressed_size / sizeof(T);

			// Number of full blocks
			size_t full_blocks = s / block_size;

			RebindAlloc<char> al = get_allocator();

			for (size_t i = 0; i < full_blocks; ++i) {
				size_t bsize = stenos_private_block_size(src, (size_t)(src_end - src));
				if STENOS_UNLIKELY (stenos_has_error(bsize))
					return bsize;
				if STENOS_UNLIKELY (src + bsize > src_end)
					return STENOS_ERROR_SRC_OVERFLOW;

				// might throw, fine
				char* data = al.allocate(bsize);
				memcpy(data, src, bsize);

				try {
					d_data->d_buckets.push_back(bucket_type());
				}
				catch (...) {
					al.deallocate(data, bsize);
					throw;
				}
				d_data->d_buckets.back().data.set(data, detail::Compressed);
				d_data->d_buckets.back().csize = bsize;
				d_data->d_size += block_size;

				src += bsize;
			}

			if (size_t rem = s % block_size) {

				// last bucket

				size_t bsize = stenos_private_block_size(src, (size_t)(src_end - src));
				if STENOS_UNLIKELY (stenos_has_error(bsize))
					return bsize;
				if STENOS_UNLIKELY (src + bsize > src_end)
					return STENOS_ERROR_SRC_OVERFLOW;

				// create a raw buffer, might throw, fine
				detail::RawBuffer<T, internal_type::elems_per_block>* raw = d_data->make_raw();
				// add to contexts
				d_data->d_contexts.push_front(raw);

				size_t r = stenos_private_decompress_block(d_data->d_ctx, src, sizeof(T), block_bytes, bsize, raw->storage, rem * sizeof(T));
				if STENOS_UNLIKELY (stenos_has_error(r))
					return r;
				if STENOS_UNLIKELY (r != rem * sizeof(T))
					return STENOS_ERROR_INVALID_INPUT;

				// might throw, fine
				d_data->d_buckets.push_back(bucket_type());
				d_data->d_buckets.back().buffer.set(raw, detail::Raw);
				d_data->d_buckets.back().csize = 0;

				raw->size = (unsigned)rem;
				raw->mark_dirty();
				d_data->d_size += raw->size;
				raw->block_index = d_data->d_buckets.size() - 1;
			}

			return size();
		}

		template<class Istream>
		size_t deserialize(Istream& iss);

		// TEST
		bool validate()
		{
			if (d_data) {
				for (size_t i = 0; i < d_data->d_buckets.size(); ++i) {
					auto val = d_data->d_buckets[i].ref_count.value();
					if (val != 0)
						return false;
				}
			}
			return true;
		}
	};

} // end namespace stenos

namespace std
{
	namespace stenos_detail
	{
		template<class T, bool MoveOnly = !std::is_copy_assignable<T>::value>
		struct MoveObject
		{
			static STENOS_ALWAYS_INLINE T apply(T&& ref) { return std::move(ref); }
		};
		template<class T>
		struct MoveObject<T, true>
		{
			static STENOS_ALWAYS_INLINE T apply(T&& ref)
			{
				T tmp = std::move(ref);
				return tmp;
			}
		};
	}

	///////////////////////////
	// Completely illegal overload of std::move.
	// That's currently the only way I found to use generic algorithms (like std::move(It, It, Dst) ) with cvector. Works with msvc, gcc and clang.
	///////////////////////////

	template<class Compress>
	STENOS_ALWAYS_INLINE typename Compress::value_type move(stenos::detail::RefWrapper<Compress>& other) noexcept
	{
		return stenos_detail::MoveObject<typename Compress::value_type>::apply(other.move());
	}

	/* template<class Compress>
	STENOS_ALWAYS_INLINE typename Compress::value_type move(stenos::detail::RefWrapper<Compress>&& other) noexcept
	{
		return stenos_detail::MoveObject<typename Compress::value_type>::apply(other.move());
	}*/
}

#include <iostream>

namespace stenos
{

	template<class T, unsigned BlockSize, int Level, class Alloc>
	template<class Ostream>
	size_t cvector<T, BlockSize, Level, Alloc>::serialize(Ostream& oss)
	{
		uint8_t header[12];
		size_t pos = oss.tellp();
		size_t r = stenos_private_create_compression_header(size() * sizeof(T), block_bytes, header, 12);
		if STENOS_UNLIKELY (stenos_has_error(r))
			return r;

		oss.write((char*)header, r);
		if STENOS_UNLIKELY (!oss)
			return STENOS_ERROR_DST_OVERFLOW;

		if (!d_data || size() == 0)
			goto end;

		// Write all compressed blocks
		for (size_t i = 0; i < d_data->d_buckets.size(); ++i) {
			// Last bucket
			if (i == d_data->d_buckets.size() - 1) {
				// check if last bucket is empty
				auto* buffer = &d_data->d_buckets.back();
				auto* raw = buffer->raw();
				if (!raw || raw->size == 0)
					goto end;

				// check if last bucket is full and compressed
				if (raw->size == block_size && raw->buffer) {
					oss.write((char*)raw->buffer, buffer->csize);
					if STENOS_UNLIKELY (!oss)
						return STENOS_ERROR_DST_OVERFLOW;
					goto end;
				}

				// compress last bucket
				r = d_data->compress(raw->storage, raw->size * sizeof(T));
				if STENOS_UNLIKELY (stenos_has_error(r))
					return r;
				oss.write((char*)d_data->compression_buffer(), r);
				if STENOS_UNLIKELY (!oss)
					return STENOS_ERROR_DST_OVERFLOW;
				goto end;
			}

			auto buf = compressed_block(i);
			if STENOS_UNLIKELY (!buf.first)
				return STENOS_ERROR_ALLOC;

			oss.write((char*)buf.first, buf.second);
			if STENOS_UNLIKELY (!oss)
				return STENOS_ERROR_DST_OVERFLOW;
		}

	end:
		return (size_t)oss.tellp() - pos;
	}

	template<class T, unsigned BlockSize, int Level, class Alloc>
	template<class Istream>
	size_t cvector<T, BlockSize, Level, Alloc>::deserialize(Istream& iss)
	{
		clear();
		make_data_if_null();

		uint8_t header[12];
		iss.read((char*)header, 12);
		if STENOS_UNLIKELY (!iss)
			return STENOS_ERROR_SRC_OVERFLOW;

		stenos_info info;
		// Read frame info
		size_t r = stenos_get_info(header, sizeof(T), 12, &info);
		if STENOS_UNLIKELY (stenos_has_error(r))
			return r;

		// Invalid superblock size
		if STENOS_UNLIKELY (block_bytes != info.superblock_size)
			return STENOS_ERROR_INVALID_INPUT;

		// Empty vector
		if STENOS_UNLIKELY (info.decompressed_size == 0)
			return 0;

		// Check decompressed size validity
		if STENOS_UNLIKELY (info.decompressed_size % sizeof(T) != 0)
			return STENOS_ERROR_INVALID_INPUT;

		size_t s = info.decompressed_size / sizeof(T);

		// Number of full blocks
		size_t full_blocks = s / block_size;

		RebindAlloc<char> al = get_allocator();

		for (size_t i = 0; i < full_blocks; ++i) {
			char bheader[4];
			iss.read((char*)bheader, 4);
			if STENOS_UNLIKELY (!iss)
				return STENOS_ERROR_SRC_OVERFLOW;

			size_t bsize = stenos_private_block_size(bheader, 4);
			if STENOS_UNLIKELY (stenos_has_error(bsize))
				return bsize;

			iss.seekg(-4, std::ios::cur);
			// might throw, fine
			char* data = al.allocate(bsize);
			iss.read(data, bsize);
			if STENOS_UNLIKELY (!iss) {
				al.deallocate(data, bsize);
				return STENOS_ERROR_SRC_OVERFLOW;
			}

			try {
				d_data->d_buckets.push_back(bucket_type());
			}
			catch (...) {
				al.deallocate(data, bsize);
				throw;
			}
			d_data->d_buckets.back().buffer.set(data, detail::Compressed);
			d_data->d_buckets.back().csize = bsize;
			d_data->d_size += block_size;
		}

		if (size_t rem = s % block_size) {

			// last bucket

			char bheader[4];
			iss.read((char*)bheader, 4);
			if STENOS_UNLIKELY (!iss)
				return STENOS_ERROR_SRC_OVERFLOW;

			size_t bsize = stenos_private_block_size(bheader, 4);
			if STENOS_UNLIKELY (stenos_has_error(bsize))
				return bsize;

			iss.seekg(-4, std::ios::cur);
			std::vector<char> buffer(bsize);
			iss.read(buffer.data(), bsize);
			if STENOS_UNLIKELY (!iss)
				return STENOS_ERROR_SRC_OVERFLOW;

			// create a raw buffer, might throw, fine
			detail::RawBuffer<T, internal_type::elems_per_block>* raw = d_data->make_raw();
			// add to contexts
			d_data->d_contexts.push_front(raw);

			size_t r = stenos_private_decompress_block(d_data->d_ctx, buffer.data(), sizeof(T), block_bytes, buffer.size(), raw->storage, rem * sizeof(T));
			if STENOS_UNLIKELY (stenos_has_error(r))
				return r;
			if STENOS_UNLIKELY (r != rem * sizeof(T))
				return STENOS_ERROR_INVALID_INPUT;

			// might throw, fine
			d_data->d_buckets.push_back(bucket_type());
			d_data->d_buckets.back().data.set(raw, detail::Raw);
			d_data->d_buckets.back().csize = 0;

			raw->size = (unsigned)rem;
			raw->mark_dirty();
			d_data->d_size += raw->size;
			raw->block_index = d_data->d_buckets.size() - 1;
		}

		return size();
	}

	/// @brief Read binary objects from a std::istream
	///
	/// stenos::istreambuf_iterator is similar to std::istreambuf_iterator
	/// but is used to read binary data of any size from a std::istream object.
	///
	template<class T>
	class istreambuf_iterator
	{
		mutable std::istream* d_in; // the input stream
		mutable bool d_has_value;
		alignas(alignof(T)) mutable char d_value[sizeof(T)]; // next element to deliver

	public:
		static_assert(is_relocatable<T>::value, "non relocatable type");

		using iterator_category = std::input_iterator_tag;
		using value_type = T;
		using difference_type = std::ptrdiff_t;
		using pointer = const T*;
		using reference = T;

		constexpr istreambuf_iterator() noexcept
		  : d_in(nullptr)
		  , d_has_value(false)
		{
		}

		istreambuf_iterator(std::istream& in) noexcept
		  : d_in(&in)
		  , d_has_value(false)
		{
		}

		istreambuf_iterator(istreambuf_iterator&& other) noexcept
		  : d_in(&other.d_in)
		  , d_has_value(other.d_has_value)
		{
			if (d_has_value)
				new (d_value) T(std::move(*other.value()));
			other.d_in = nullptr;
		}

		~istreambuf_iterator() noexcept
		{
			if STENOS_CONSTEXPR (!std::is_trivially_destructible<T>::value) {
				if (d_has_value)
					value()->~T();
			}
		}
		T operator*() const
		{
			if (!d_has_value)
				read();

			STENOS_ASSERT_DEBUG(d_in, "istreambuf_iterator is not dereferenceable");
			return *value();
		}

		istreambuf_iterator& operator++()
		{
			STENOS_ASSERT_DEBUG(d_in, "istreambuf_iterator is not incrementable");
			increment();
			return *this;
		}

		bool equal(const istreambuf_iterator& right) const
		{
			if (!d_has_value && d_in)
				read();

			if (!right.d_has_value && right.d_in)
				right.read();

			return (!d_in && !right.d_in) || (d_in && right.d_in);
		}

	private:
		T* value() noexcept { return reinterpret_cast<T*>(d_value); }
		const T* value() const noexcept { return reinterpret_cast<const T*>(d_value); }

		void increment()
		{
			// skip to next input element
			if (d_in) {
				if (d_has_value) {
					d_has_value = false;
					value()->~T();
				}
				if (d_in->read(d_value, sizeof(d_value))) {
					d_has_value = true;
					return;
				}
				d_in = nullptr;
			}
		}

		bool read() const
		{
			STENOS_ASSERT_DEBUG(!d_has_value, "");

			// read at next input element
			if (d_in) {
				if (d_in->read(d_value, sizeof(d_value))) {
					d_has_value = true;
					return true;
				}
				d_in = nullptr;
			}
			return false;
		}
	};

	template<class T>
	bool operator==(const istreambuf_iterator<T>& left, const istreambuf_iterator<T>& right)
	{
		return left.equal(right);
	}
	template<class T>
	bool operator!=(const istreambuf_iterator<T>& left, const istreambuf_iterator<T>& right)
	{
		return !(left == right);
	}
}

#endif
