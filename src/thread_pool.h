#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include "core.h"

#include <optional>
#include <future>
#include <thread>
#include <semaphore>

struct thread_pool
{
	thread_pool(std::size_t thread_count)
		: _thread_count(thread_count),
		  _task_wait_semaphore(0), // we don't have any tasks yet, so we start with 0
		  _tasks_mutex(),
		  _threads(),
		  _tasks()
	{
		this->_threads.reserve(thread_count);
	}

	~thread_pool(void)
	{
		{
			auto const tasks_guard = std::lock_guard(this->_tasks_mutex);
			if (this->_tasks.empty())
			{
				// all queued tasks have finished, so we need to notify the worker threads to shut down
				this->_task_wait_semaphore.release();
			}
			else
			{
				// some tasks haven't finished yet, so we just discard them
				this->_tasks.clear();
				// the semaphore doesn't need to be released here
			}
		}
		// the threads should clean themselves up here
		// this needs to happen before the destructor finishes, because the threads call 'get_next_task()'
		this->_threads.clear();
	}

	// called from main thread
	auto push_task(auto callable) -> std::future<decltype(callable())>
	{
		using R = decltype(callable());
		auto const tasks_guard = std::lock_guard(this->_tasks_mutex);

		// we fill the thread pool here, to avoid unnecessary thread starting
		if (this->_threads.size() < this->_thread_count)
		{
			this->_threads.push_back(std::jthread([this]() {
				while (auto task = this->get_next_task())
				{
					(*task)();
				}
			}));
		}

		// the promise needs to be copyable, because we convert it to a 'std::function' object,
		// so we use a 'shared_ptr'
		auto promise = std::make_shared<std::promise<R>>();
		auto result = promise->get_future();
		this->_tasks.push_back([promise = std::move(promise), callable = std::move(callable)]() {
			promise->set_value(callable());
		});

		// release if a new task is pushed into an empty queue
		if (this->_tasks.size() == 1)
		{
			this->_task_wait_semaphore.release();
		}

		return result;
	}

	// called from worker threads
	std::optional<std::function<void()>> get_next_task()
	{
		// try to acquire the semaphore
		// blocks until there is at least one task in the queue, or the thread_pool is destructed
		this->_task_wait_semaphore.acquire();
		auto const tasks_guard = std::lock_guard(this->_tasks_mutex);

		if (this->_tasks.empty())
		{
			this->_task_wait_semaphore.release();
			return {};
		}
		else if (this->_tasks.size() == 1)
		{
			// don't release, as we don't have any more tasks
			auto result = std::move(this->_tasks[0]);
			this->_tasks.erase(this->_tasks.begin());
			return result;
		}
		else
		{
			auto result = std::move(this->_tasks[0]);
			this->_tasks.erase(this->_tasks.begin());
			// release the semaphore, as there are more tasks left in the queue
			this->_task_wait_semaphore.release();
			return result;
		}
	}

private:
	std::size_t _thread_count;
	std::binary_semaphore _task_wait_semaphore;
	std::mutex _tasks_mutex;
	cppb::vector<std::jthread> _threads;
	cppb::vector<std::function<void()>> _tasks;
};

#endif // CONFIG_H
