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
#include <atomic>

using namespace std;

mutex cout_mutex;

atomic<long long> total_wait_time_ms{ 0 };
atomic<int> completed_tasks_count{ 0 };

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

    int active_tasks{ 0 };
    condition_variable cv_active_tasks_finished;

    atomic<long long> total_wait_time_sec{ 0 };
    atomic<int> wait_count{ 0 };

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

    void stop_immediately()
    {
        {
            unique_lock<mutex> lock(queue_mutex);
            stop = true;

            active_tasks -= tasks.size();

            queue<function<void()>> empty_queue;
            swap(tasks, empty_queue);
        }

        cv.notify_all();
    }

    size_t get_threads_count() const
    {
        return workers.size();
    }

    double get_avg_wait_time() const
    {
        if (wait_count == 0) return 0.0;

        return static_cast<double>(total_wait_time_sec) / wait_count / 1000.0;
    }

    void wait_all()
    {
        unique_lock<mutex> lock(queue_mutex);
        cv_active_tasks_finished.wait(lock, [this]() {
            return active_tasks == 0;
        });
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

            auto start_wait = chrono::high_resolution_clock::now();

            cv.wait(lock, [this]() {
                return stop || (!tasks.empty() && !paused);
            });

            auto end_wait = chrono::high_resolution_clock::now();
            long long slept = chrono::duration_cast<chrono::milliseconds>(end_wait - start_wait).count();

            if (slept > 0)
            {
                total_wait_time_sec += slept;
                wait_count++;
            }
            
            if (stop && tasks.empty())
            {
                return;
            }

            task = move(tasks.front());
            tasks.pop();
        }

        task();

        {
            unique_lock<mutex> lock(queue_mutex);
            active_tasks--;

            if (active_tasks == 0)
            {
                cv_active_tasks_finished.notify_all();
            }
        }
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
        active_tasks++;
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

    total_wait_time_ms += sleep_time;
    completed_tasks_count++;

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

    pool.wait_all();

    now = chrono::system_clock::now();
    cout << format("\n[{:%H:%M:%S}] --- STATS ---\n", now);
    cout << format("Created threads amount: {}\n", pool.get_threads_count());
    cout << format("Average thread wait time: {:.2f} seconds\n", pool.get_avg_wait_time());

    double avg_task_time = 0.0;
    if (completed_tasks_count > 0)
    {
        avg_task_time = static_cast<double>(total_wait_time_ms) / completed_tasks_count;
    }

    cout << format("Average task completion time: {:.2f} seconds\n", avg_task_time);

    return 0;
}