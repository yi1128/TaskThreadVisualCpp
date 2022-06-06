#pragma once
/*
    BaseThreadPool.h
    std:: 기반 Task, TaskPriorityQueue, Worker(Thread 관리)
    PriorityThreadPool.h 기반이 되는 헤더
    
    class 목록
    class Task
    class TaskPriorityQueue
    class Worker
*/


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
//#include <time.h>

enum class TaskPriority : unsigned char 
{
    DEFAULT = 0,
    LOW,
    MEDIUM,
    HIGH,
    URGENT,
};

class Task 
{
public:
    using TaskType = std::function<void()>;
    explicit Task(TaskType task_type, TaskPriority priority)
        : m_taskType(task_type), m_priority(priority) { }
    Task(const Task& other)
        : m_taskType(other.m_taskType), m_priority(other.m_priority) {  }

    ~Task() {}

    Task& operator=(const Task& other) 
    {
        m_taskType = other.m_taskType;
        m_priority = other.m_priority;
        return *this;
    }

    bool operator<(const Task& rhs) const { return m_priority < rhs.m_priority; }
    bool operator>(const Task& rhs) const { return m_priority > rhs.m_priority; }

    TaskPriority GetPriority() const { return m_priority; }
    
    void Run() { m_taskType(); }

private:
    TaskType m_taskType;
    TaskPriority m_priority;
};

class TaskPriorityQueue 
{
public:
    explicit TaskPriorityQueue(const char* queue_name)
        : m_queueName(queue_name), m_aAlive(true), m_ulTaskCount(0), m_ulPendingTaskCount(0) { }
    
    TaskPriorityQueue(TaskPriorityQueue&& other) = delete;
    TaskPriorityQueue(const TaskPriorityQueue&) = delete;
    TaskPriorityQueue& operator=(TaskPriorityQueue&& other) = delete;
    TaskPriorityQueue& operator=(const TaskPriorityQueue&) = delete;
    ~TaskPriorityQueue() { ClearQueue(); }

    void ClearQueue() 
    {
        {
            std::unique_lock<std::mutex> lock(m_queueMtx);
            m_aAlive = false;
        }
        m_queueCv.notify_all();
        auto task = dequeue();
        while (task) {
            task->Run();
            task = dequeue();
        }
    }
    
    bool empty() const 
    {
        std::unique_lock<std::mutex> lock(m_queueMtx);
        return m_pqTasks.empty();
    }
    
    std::size_t size() const 
    {
        std::unique_lock<std::mutex> lock(m_queueMtx);
        return m_pqTasks.size();
    }
    
    template<class F, class... Args>
    auto enqueue(TaskPriority priority, F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type> 
    {
        using ReturnType = typename std::result_of<F(Args...)>::type;
        auto task = std::make_shared< std::packaged_task<ReturnType()> >(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        {
            std::unique_lock<std::mutex> lock(m_queueMtx);
            m_pqTasks.emplace([task]() { (*task)(); }, priority);
            m_ulTaskCount += 1;
            m_ulPendingTaskCount += 1;
        }
        m_queueCv.notify_one();
        return task->get_future();
    }

    std::unique_ptr<Task> dequeue() 
    {
        std::unique_lock<std::mutex> lock(m_queueMtx);
        bool bStatus = m_queueCv.wait_for(lock, std::chrono::seconds(5), [this] { return !m_aAlive || !m_pqTasks.empty(); });
        if (!bStatus || (!m_aAlive && m_pqTasks.empty())) 
        {
            return nullptr;
        }
        auto task = std::unique_ptr<Task>{ new Task(std::ref(m_pqTasks.top())) };
        m_pqTasks.pop();
        m_ulPendingTaskCount -= 1;
        return task;
    }
    const char* GetName() const { return m_queueName.c_str(); }
    uint64_t GetTaskCount() const { return m_ulTaskCount; }
    uint64_t GetPendingTaskCount() const { return m_ulPendingTaskCount; }

private:
    std::string m_queueName;
    std::priority_queue<Task> m_pqTasks;
    mutable std::mutex m_queueMtx;
    mutable std::condition_variable m_queueCv;
    std::atomic_bool m_aAlive;

    uint64_t m_ulTaskCount;
    uint64_t m_ulPendingTaskCount;
};

class Worker 
{
public:
    enum class State : unsigned char 
    {
        IDLE = 0,
        BUSY,
        EXITED,
    };

    explicit Worker(TaskPriorityQueue* queue)
        : m_state(State::IDLE), m_ulCompletedTaskCount(0) 
    {
        m_thread = std::thread([queue, this]() 
            {
                while (true) 
                {
                    auto task = queue->dequeue();
                    if (task) 
                    {
                        m_state = State::BUSY;
                        task->Run();
                        m_ulCompletedTaskCount += 1;
                    }
                    else 
                    {
                        m_state = State::EXITED;
                        return;
                    }
                }
            });
    }

    //~Worker() { SetJoinWorker(); }

    void SetJoinWorker() 
    {
        if (m_thread.joinable()) 
            m_thread.join();

    }

    State GetState() const { return m_state; }
    uint64_t GetCompletedTaskCount() const { return m_ulCompletedTaskCount; }

private:
    std::thread m_thread;
    State m_state;
    uint64_t m_ulCompletedTaskCount;
};