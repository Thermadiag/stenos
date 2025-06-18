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

#ifndef STENOS_TINY_POOL_H
#define STENOS_TINY_POOL_H

#include "../bits.hpp"
#include <mutex>
#include <type_traits>
#include <condition_variable>
#include <vector>
#include <thread>
#include <atomic>
#include <functional>
#include <queue>

namespace stenos
{

	namespace detail
	{
		// TaskBuffer used to allocate tasks
		struct TaskBuffer
		{
			TaskBuffer* next = nullptr; // next in linked list
			size_t size = 0;	    // data size
			void* data() noexcept { return this + 1; }
		};

		// Thread safe allocation/deallocation of buffers for task allocation
		struct TaskAllocation
		{

			std::atomic<TaskBuffer*> first{ nullptr };

			~TaskAllocation() noexcept
			{
				// Free all buffers
				while (TaskBuffer* b = pop())
					free(b);
			}

			TaskBuffer* pop() noexcept
			{
				// Extract a buffer from the list
				auto f = first.load(std::memory_order_relaxed);
				while (f && !first.compare_exchange_strong(f, f->next))
					;
				return f;
			}

			void* allocate(size_t s) noexcept
			{
				// Extract (and free) buffers until we find one of the right size.
				// This might clear the list. Since the thread pool only uses 3
				// different types of task within stenos, The list will quickly
				// only contain buffers of the right size.
				while (TaskBuffer* b = pop()) {
					if (b->size >= s)
						return b->data();
					free(b);
				}

				// Allocate
				TaskBuffer* res = (TaskBuffer*)malloc(s + sizeof(TaskBuffer));
				if (res)
					new (res) TaskBuffer{ nullptr, s };
				return res->data();
			}

			void deallocate(void* p) noexcept
			{
				// Insert the buffer in the list
				TaskBuffer* b = (TaskBuffer*)((uint8_t*)p - sizeof(TaskBuffer));
				auto f = first.load(std::memory_order_relaxed);
				b->next = f;
				while (!first.compare_exchange_strong(f, b)) {
					b->next = f;
				}
			}

			static STENOS_ALWAYS_INLINE TaskAllocation& instance()
			{
				static TaskAllocation alloc;
				return alloc;
			}
		};

		static inline void* allocate_task(size_t s)
		{
			return TaskAllocation::instance().allocate(s);
		}
		static inline void deallocate_task(void* p)
		{
			TaskAllocation::instance().deallocate(p);
		}

		// Base task class
		struct BaseTask
		{
			BaseTask* left = nullptr;
			BaseTask* right = nullptr;
			virtual ~BaseTask() noexcept {}
			virtual void apply() noexcept {};

			void insert(BaseTask* l, BaseTask* r) noexcept
			{
				this->left = l;
				this->right = r;
				l->right = r->left = this;
			}
			void remove() noexcept
			{
				this->left->right = this->right;
				this->right->left = this->left;
			}
		};

		// Concrete task
		template<class T>
		struct Task : public BaseTask
		{
			T task;
			Task(T&& u)
			  : task(std::forward<T>(u))
			{
			}
			virtual void apply() noexcept
			{
				try {
					task();
				}
				catch (...) {
				}
			};
		};

		// List of tasks
		class TaskList
		{
			BaseTask end;

		public:
			TaskList() noexcept { end.left = end.right = &end; }

			~TaskList() noexcept
			{
				auto* t = end.right;
				while (t != &end) {
					auto* next = t->right;
					destroy_task(t);
					t = next;
				}
			}

			template<class U>
			static BaseTask* make_task(U&& u) noexcept
			{
				try {
					Task<U>* t = (Task<U>*)allocate_task(sizeof(Task<U>));
					return new (t) Task<U>(std::forward<U>(u));
				}
				catch (...) {
					return nullptr;
				}
			}

			static void destroy_task(BaseTask* t) noexcept
			{
				t->~BaseTask();
				deallocate_task(t);
			}

			void push_back(BaseTask* t) noexcept { t->insert(end.left, &end); }

			void push_back(TaskList* lst) noexcept
			{
				if (!lst->empty()) {
					lst->end.right->left = end.left;
					lst->end.left->right = &end;

					end.left->right = lst->end.right;
					end.left = lst->end.left;

					lst->end.right = lst->end.left = &lst->end;
				}
			}

			BaseTask* pop_front() noexcept
			{
				auto r = end.right;
				r->remove();
				return r;
			}

			bool empty() const noexcept { return &end == end.right; }
		};
	}

	/// @brief Minimalist thread pool class.
	/// Used to launch compression/decompression jobs using
	/// a global pool created at program initialization.
	///
	/// Uses a unique queue for all threads as this pattern
	/// works well in our situation.
	class tiny_pool
	{
		std::mutex mutex;
		std::condition_variable condition;
		std::condition_variable wait_condition;
		detail::TaskList list;
		std::vector<std::thread> threads;
		bool finish = false;
		bool waiting = false;
		unsigned processing = 0;

		void do_work() noexcept
		{
			while (!finish) {
				std::unique_lock<std::mutex> lock(mutex);
				--processing;

				if (waiting && processing == 0 && list.empty())
					wait_condition.notify_all();

				condition.wait(lock, [this] { return (!list.empty()) || this->finish; });
				if STENOS_UNLIKELY (this->finish)
					break;

				auto r = list.pop_front();
				++processing;
				lock.unlock();

				r->apply();
				list.destroy_task(r);
			}
		}

	public:
		// Constructor, only function that might throw
		tiny_pool(unsigned nthreads)
		  : threads(nthreads)
		  , processing(nthreads)
		{
			if (nthreads == 0) {
				processing = 1;
				threads.resize(1);
			}
			for (unsigned i = 0; i < nthreads; ++i)
				threads[i] = std::thread([this]() { this->do_work(); });
		}
		~tiny_pool() noexcept
		{
			{
				std::unique_lock<std::mutex> lock(mutex);
				finish = true;
				condition.notify_all();
			}
			for (size_t i = 0; i < threads.size(); ++i)
				threads[i].join();
		}

		void wait() noexcept
		{
			std::unique_lock<std::mutex> lock(mutex);
			waiting = true;
			wait_condition.wait(lock, [this] { return (this->processing == 0) && this->list.empty(); });
			waiting = false;
		}

		template<class U>
		bool push(U&& u) noexcept
		{
			auto t = list.make_task(std::forward<U>(u));
			if (t) {
				std::lock_guard<std::mutex> lock(mutex);
				list.push_back(t);
			}
			condition.notify_one();
			return t;
		}
	};
}

#endif