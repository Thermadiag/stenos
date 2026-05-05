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

#ifdef min
#undef min
#undef max
#endif

namespace stenos
{

	struct task_t
	{
		std::function<void()> fun;
		std::atomic<int>* sentinel = nullptr;

		void call() noexcept
		{
			try {
				fun();
			}
			catch (...) {
			}
			if (sentinel)
				sentinel->fetch_sub(1, std::memory_order_relaxed);
		}
	};

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
		std::queue<task_t> list;
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

				auto r = list.front();
				list.pop();
				++processing;
				lock.unlock();

				r.call();
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

		void wait(std::atomic<int>* sentinel = nullptr) noexcept
		{
			std::unique_lock<std::mutex> lock(mutex);
			waiting = true;
			wait_condition.wait(lock, [&] {
				if (!sentinel)
					return (this->processing == 0) && this->list.empty();
				else
					return sentinel->load(std::memory_order_relaxed) == 0;
			});
			waiting = false;
		}

		template<class U>
		bool push(U&& u, std::atomic<int>* sentinel = nullptr) noexcept
		{
			try {
				task_t t;
				t.fun = std::forward<U>(u);
				t.sentinel = sentinel;
				std::lock_guard<std::mutex> lock(mutex);
				list.emplace(std::move(t));
				condition.notify_one();
				return true;
			}
			catch (...) {

				return false;
			}
		}

		template<class U>
		bool loop_for(int thread_count, int start, int end, int step, U u) noexcept
		{
			if (thread_count <= 1) {
				for (; start < end; start += step) {
					u(start);
				}
				return true;
			}
			//  Adjust thread count
			thread_count = std::min(thread_count, (int)threads.size());
			// Compute range
			int count = (end - start);
			// Compute number of blocks
			int block_count = std::min(count / step, thread_count);
			if (block_count == 0) {
				if (end == start) {
					// Nothing to do
					return true;
				}
				block_count = 1;
			}
			// Compute block size
			int block_size = count / block_count;
			if (block_count > 1 && (block_size % step) != 0) {
				block_size = (block_size / step) * step;
				block_count = count / block_size + (count % block_size ? 1 : 0);
			}

			std::atomic<int> sentinel{ block_count-1 };
			for (int i = 0; i < block_count; ++i) {
				auto fun = [&, i]() {
					int first = start + i * block_size;
					int last = (i == block_count - 1) ? end : first + block_size;
					for (; first < last; first += step)
						u(first);
				};
				if (i == block_count - 1) {
					try {
						fun();
					}
					catch (...) {
						wait();
						return false;
					}
				}
				else {
					if (!push(fun, &sentinel)) {
						wait();
						return false;
					}
				}
			}
			wait(&sentinel);
			return true;
		}
	};

	static inline tiny_pool& get_pool()
	{
		// Create static thread pool
		static tiny_pool pool(std::thread::hardware_concurrency() * 2u);
		return pool;
	}

}

#endif