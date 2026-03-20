#include <iostream>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>

using namespace std;

class ThreadPool
{
private:
    vector<thread> workers;
    queue<function<void()>> tasks;

    mutex queue_mutex;
	condition_variable cv;

	bool stop;

    void worker_routine();
public:
	ThreadPool(size_t num_threads = 8);

    ~ThreadPool();

    bool is_stopped() const
    {
        return stop;
    }

    template<class F, class... Args> void add_task(F&& f, Args&&... args);
};

void ThreadPool::worker_routine()
{
    while (true)
    {
        function<void()> task;

        {
            unique_lock<mutex> lock(queue_mutex);

            cv.wait(lock, [this]() {
				return stop || !tasks.empty();
            });

			if (stop && tasks.empty())
            {
                return;
            }

            task = move(tasks.front());
            tasks.pop();
        }

        task();
    }
}

ThreadPool::ThreadPool(size_t num_threads) : stop(false)
{
    for(size_t i = 0; i < num_threads; ++i)
    {
        workers.emplace_back(&ThreadPool::worker_routine, this);
    }
}

ThreadPool::~ThreadPool()
{
    {
        unique_lock<mutex> lock(queue_mutex);
        stop = true;
    }

    cv.notify_all();

    for (thread& worker : workers)
    {
        if (worker.joinable())
        {
			worker.join();
        }
    }
}

template<class F, class... Args> void add_task(F&& f, Args&&... args)
{
    auto task = bind(forward<F>(f), forward<Args>(args)...);

    {
        unique_lock<mutex> lock(queue_mutex);

        if (stop)
        {
            throw runtime_error("Adding task to ThreadPool has been stopped");
        }

        task.emplace(task);
    }

	cv.notify_one();
}

int main()
{
    
}