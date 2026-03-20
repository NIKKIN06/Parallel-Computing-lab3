#include <iostream>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <chrono>
#include <random>
#include <format>
#include <sstream>

using namespace std;

mutex cout_mutex;

string get_thread_id()
{
    stringstream ss;
    ss << this_thread::get_id();
    return ss.str();
}

class ThreadPool
{
private:
    vector<thread> workers;
    queue<function<void()>> tasks;

    mutex queue_mutex;
	condition_variable cv;

	bool stop;
    bool paused;

    void worker_routine();
public:
	ThreadPool(size_t num_threads = 8);

    ~ThreadPool();

    void pause()
    {
        unique_lock<mutex> lock(queue_mutex);
        paused = true;
    }

    void resume()
    {
        {
            unique_lock<mutex> lock(queue_mutex);
            paused = false;
        }

        cv.notify_all();
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
                return stop || (!tasks.empty() && !paused);
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

ThreadPool::ThreadPool(size_t num_threads) : stop(false), paused(false)
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

template<class F, class... Args> void ThreadPool::add_task(F&& f, Args&&... args)
{
    auto task = bind(forward<F>(f), forward<Args>(args)...);

    {
        unique_lock<mutex> lock(queue_mutex);

        if (stop)
        {
            throw runtime_error("Adding task to ThreadPool has been stopped");
        }

        tasks.push(function<void()>(task));
    }

	cv.notify_one();
}

void work(int task_id)
{
    {
        lock_guard<mutex> lock(cout_mutex);

        auto now = chrono::system_clock::now();

        cout << format("[{:%H:%M:%S}] [Thread {}] has got a task {}\n", now, get_thread_id(), task_id);
    }

    random_device rd;
    mt19937 gen(rd());
    uniform_int_distribution<> dist(10, 16);
    int sleep_time = dist(gen);

    this_thread::sleep_for(chrono::seconds(sleep_time));

    {
        lock_guard<mutex> lock(cout_mutex);

        auto now = chrono::system_clock::now();

        cout << format("[{:%H:%M:%S}] [Thread {}] has finished a task {} (Time spent: {} seconds)\n", now, get_thread_id(), task_id, sleep_time);
    }
}

void task_producer(ThreadPool& pool, int producer_id, int start_task_id, int count)
{
    for (int i = 0; i < count; i++)
    {
        int task_id = start_task_id + i;

        {
            lock_guard<mutex> lock(cout_mutex);

            auto now = chrono::system_clock::now();

            cout << format("[{:%H:%M:%S}] [Generator {}] has added a task {}\n", now, get_thread_id(), task_id);
        }

        pool.add_task(work, task_id);

        this_thread::sleep_for(chrono::milliseconds(500));
    }
}

int main()
{
    ThreadPool pool(8);
    
    thread producer1(task_producer, ref(pool), 1, 10, 4);
    thread producer2(task_producer, ref(pool), 2, 20, 4);
    thread producer3(task_producer, ref(pool), 3, 30, 4);

    producer1.join();
    producer2.join();
    producer3.join();

    auto now = chrono::system_clock::now();

    cout << format("[{:%H:%M:%S}] All generators did their work. Testing pause...\n", now);

    pool.pause();
    now = chrono::system_clock::now();
    cout << format("\n[{:%H:%M:%S}] Pool is paused, no new tasks are being accepted...\n\n", now);
    this_thread::sleep_for(chrono::seconds(20));

    now = chrono::system_clock::now();
    cout << format("\n[{:%H:%M:%S}] Pool is resumed. Work is continued...\n\n", now);
    pool.resume();

    return 0;
}