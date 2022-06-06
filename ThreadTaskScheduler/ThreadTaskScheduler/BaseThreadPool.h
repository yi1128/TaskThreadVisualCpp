#pragma once

#include <cstdint>
#include <ctime>
#include <iostream>
#include <iomanip>
#include <string>
#include <sstream>
#include <queue>
#include <vector>
#include <map>
#include <algorithm>
#include <memory>
#include <mutex>
#include <functional>
#include <future>
#include <chrono>

// localtimer_r
#include <time.h>

enum class TaskPriority : unsigned char {
    DEFAULT = 0,
    LOW,
    MEDIUM,
    HIGH,
    URGENT,
};

namespace detail {

    uint8_t g_auto_increment_thread_pool_id = 0;

}

class Task {
public:
    using TaskType = std::function<void()>;
    explicit Task(TaskType task, TaskPriority priority)
        : task_(task), priority_(priority) {
    }
    Task(const Task& other)
        : task_(other.task_), priority_(other.priority_) {
    }
    Task& operator=(const Task& other) {
        task_ = other.task_;
        priority_ = other.priority_;
        return *this;
    }

    bool operator<(const Task& rhs) const {
        return priority_ < rhs.priority_;
    }
    bool operator>(const Task& rhs) const {
        return priority_ > rhs.priority_;
    }

    TaskPriority priority() const { return priority_; }
    void Run() {
        task_();
    }

private:
    TaskType task_;
    TaskPriority priority_;
};

class TaskPriorityQueue {
public:
    explicit TaskPriorityQueue(const char* queue_name)
        : queue_name_(queue_name), alive_(true), task_count_(0), pending_task_count_(0) {
    }
    TaskPriorityQueue(TaskPriorityQueue&& other) = delete;
    TaskPriorityQueue(const TaskPriorityQueue&) = delete;
    TaskPriorityQueue& operator=(TaskPriorityQueue&& other) = delete;
    TaskPriorityQueue& operator=(const TaskPriorityQueue&) = delete;
    ~TaskPriorityQueue() {
        ClearQueue();
    }
    void ClearQueue() {
        {
            std::unique_lock<std::mutex> lock(queue_mtx_);
            alive_ = false;
        }
        queue_cv_.notify_all();
        auto task = dequeue();
        while (task) {
            task->Run();
            task = dequeue();
        }
    }
    bool empty() const {
        std::unique_lock<std::mutex> lock(queue_mtx_);
        return tasks_.empty();
    }
    std::size_t size() const {
        std::unique_lock<std::mutex> lock(queue_mtx_);
        return tasks_.size();
    }
    template<class F, class... Args>
    auto enqueue(TaskPriority priority, F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type> 
    {
        using ReturnType = typename std::result_of<F(Args...)>::type;
        auto task = std::make_shared< std::packaged_task<ReturnType()> >(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        {
            std::unique_lock<std::mutex> lock(queue_mtx_);
            tasks_.emplace([task]() { (*task)(); }, priority);
            task_count_ += 1;
            pending_task_count_ += 1;
        }
        queue_cv_.notify_one();
        return task->get_future();
    }
    std::unique_ptr<Task> dequeue() 
    {
        std::unique_lock<std::mutex> lock(queue_mtx_);
        bool status = queue_cv_.wait_for(lock, std::chrono::seconds(5), [this] { return !alive_ || !tasks_.empty(); });
        if (!status || (!alive_ && tasks_.empty())) {
            return nullptr;
        }
        auto task = std::unique_ptr<Task>{ new Task(std::ref(tasks_.top())) };
        tasks_.pop();
        pending_task_count_ -= 1;
        return task;
    }
    const char* name() const { return queue_name_.c_str(); }
    uint64_t task_count() const { return task_count_; }
    uint64_t pending_task_count() const { return pending_task_count_; }

private:
    std::string queue_name_;
    std::priority_queue<Task> tasks_;
    mutable std::mutex queue_mtx_;
    mutable std::condition_variable queue_cv_;
    std::atomic_bool alive_;

    uint64_t task_count_;
    uint64_t pending_task_count_;
};

class Worker {
public:
    enum class State : unsigned char {
        IDLE = 0,
        BUSY,
        EXITED,
    };
    explicit Worker(TaskPriorityQueue* queue)
        : state_(State::IDLE), completed_task_count_(0) {
        t_ = std::thread([queue, this]() {
            while (true) {
                auto task = queue->dequeue();
                if (task) {
                    state_ = State::BUSY;
                    task->Run();
                    completed_task_count_ += 1;
                }
                else {
                    state_ = State::EXITED;
                    return;
                }
            }
            });
    }
    void Work() {
        if (t_.joinable()) {
            t_.join();
        }
    }
    State state() const { return state_; }
    uint64_t completed_task_count() const { return completed_task_count_; }

private:
    std::thread t_;
    State state_;
    uint64_t completed_task_count_;
};