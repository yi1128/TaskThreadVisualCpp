#pragma once
#include "BaseThreadPool.h"

class ClassifyThreadPool {
public:
    ClassifyThreadPool(const char* name, uint16_t capacity)
        : id_(++detail::g_auto_increment_thread_pool_id), name_(name), capacity_(capacity) {
        workers_.reserve(capacity);
        ConnectTaskPriorityQueue();
    }

    ClassifyThreadPool(ClassifyThreadPool&&) = delete;
    ClassifyThreadPool(const ClassifyThreadPool&) = delete;
    ClassifyThreadPool& operator=(ClassifyThreadPool&&) = delete;
    ClassifyThreadPool& operator=(const ClassifyThreadPool&) = delete;

    void InitWorkers(uint16_t count) {
        for (unsigned char i = 0; i < count && workers_.size() < capacity_; ++i) {
            AddWorker();
        }
    }
    uint8_t id() const { return id_; }
    const char* name() const { return name_.c_str(); }
    uint16_t capacity() const { return capacity_; }
    uint16_t WorkerCount() const { return static_cast<uint16_t>(workers_.size()); }
    uint16_t IdleWorkerCount() const {
        return GetWorkerStateCount(Worker::State::IDLE);
    }
    uint16_t BusyWorkerCount() const {
        return GetWorkerStateCount(Worker::State::BUSY);
    }
    uint16_t ExitedWorkerCount() const {
        return GetWorkerStateCount(Worker::State::EXITED);
    }
    const std::vector<Worker>& workers() const { return workers_; }
    const std::unique_ptr<TaskPriorityQueue>& task_queue() const { return task_queue_; }

private:
    friend class SmartThreadPool;
    void ConnectTaskPriorityQueue() {
        std::string queue_name = name_ + "-->TaskQueue";
        task_queue_ = std::unique_ptr<TaskPriorityQueue>{ new TaskPriorityQueue(queue_name.c_str()) };
    }
    void AddWorker() {
        workers_.emplace_back(task_queue_.get());
    }
    void StartWorkers() {
        for (auto& worker : workers_) {
            worker.Work();
        }
    }
    uint16_t GetWorkerStateCount(Worker::State state) const {
        uint16_t count = 0;
        for (auto& worker : workers_) {
            if (worker.state() == state) {
                count += 1;
            }
        }
        return count;
    }
    uint8_t id_;
    std::string name_;
    uint16_t capacity_;
    std::vector<Worker> workers_;
    std::unique_ptr<TaskPriorityQueue> task_queue_;
};

class SmartThreadPool {
public:
    SmartThreadPool(SmartThreadPool&&) = delete;
    SmartThreadPool(const SmartThreadPool&) = delete;
    SmartThreadPool& operator=(SmartThreadPool&&) = delete;
    SmartThreadPool& operator=(const SmartThreadPool&) = delete;

    template<class F, class... Args>
    auto ApplyAsync(const char* pool_name, TaskPriority priority,
        F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type> {
        auto& pool = pools_.at(pool_name);
        auto res = pool->task_queue()->enqueue(priority, f, args...);
        if (pool->task_queue()->size() >= pool->WorkerCount()
            && pool->WorkerCount() < pool->capacity()) {
            pool->AddWorker();
        }
        return res;
    }
    void StartAllWorkers() {
        for (auto&& pool : pools_) {
            pool.second->StartWorkers();
        }
    }

private:
    friend class SmartThreadPoolBuilder;
    friend class Monitor;
    SmartThreadPool() {}
    std::map<std::string, std::unique_ptr<ClassifyThreadPool> > pools_;
};

class Monitor {
public:
    void StartMonitoring(const SmartThreadPool& pool, const std::chrono::duration<int>& monitor_second_period) {
        t_ = std::move(std::thread([&pool, &monitor_second_period, this]() {
            while (true) {
                std::this_thread::sleep_for(monitor_second_period);
                for (auto&& pool_map : pool.pools_) {
                    auto& classify_pool = *pool_map.second.get();
                    MonitorClassifyPool(classify_pool);
                }

                char now[128];
                std::time_t curTime = std::time(NULL);
                struct tm tmCurTime;

                errno_t e = localtime_s(&tmCurTime, &curTime);
                if (e != false)
                    return;

                std::strftime(now, sizeof(now), "%F %T", &tmCurTime);
                std::string now_str(now);

                std::stringstream monitor_log;
                auto cmp = [](const std::string& s1, const std::string& s2) { return s1.size() < s2.size(); };
                size_t max_row_msg_length = 0;

                for (size_t i = 0; i < pool_msgs_.size(); ++i) {
                    int max_pool_msg_length = std::max_element(pool_msgs_.begin(), pool_msgs_.end(), cmp)->length();
                    int max_workers_msg_length = std::max_element(workers_msgs_.begin(), workers_msgs_.end(), cmp)->length();
                    max_pool_msg_length += 2;
                    max_workers_msg_length += 2;
                    std::stringstream row_log;
                    row_log << std::left << std::setw(max_pool_msg_length) << pool_msgs_.at(i)
                        << std::left << std::setw(max_workers_msg_length) << workers_msgs_.at(i)
                        << std::left << tasks_msgs_.at(i) << std::endl;
                    if (row_log.str().length() > max_row_msg_length) {
                        max_row_msg_length = row_log.str().length();
                    }
                    monitor_log << row_log.str();
                }

                int head_front_length = (max_row_msg_length - now_str.length()) / 2;
                int head_back_length = max_row_msg_length - now_str.length() - head_front_length;
                
                // 상태 메세지
                std::stringstream pretty_msg;
                pretty_msg << "/" << std::setfill('-') << std::setw(head_front_length)
                    << "" << now << std::setfill('-') << std::setw(head_back_length)
                    << "\\" << std::endl
                    << monitor_log.str()
                    << "\\" << std::setfill('-') << std::setw(max_row_msg_length - 1)
                    << "/" << std::endl;
                
                // 상태 메세지 출력
                std::cerr << pretty_msg.str();

                pool_msgs_.clear();
                workers_msgs_.clear();
                tasks_msgs_.clear();
            }
            }));

        t_.detach();
    }

private:
    friend class SmartThreadPoolBuilder;
    Monitor() {}
    void MonitorClassifyPool(const ClassifyThreadPool& classify_pool) {
        uint16_t busy_worker = classify_pool.BusyWorkerCount();
        uint16_t idle_worker = classify_pool.IdleWorkerCount();
        uint16_t exited_worker = classify_pool.ExitedWorkerCount();
        uint16_t total_worker = classify_pool.capacity();
        uint16_t assignable_worker = total_worker - classify_pool.WorkerCount();

        uint64_t total_task = classify_pool.task_queue()->task_count();
        uint64_t running_task = classify_pool.BusyWorkerCount();
        uint64_t pending_task = classify_pool.task_queue()->pending_task_count();
        uint64_t completed_task = total_task - running_task - pending_task;

        char pool_msg[64];
        char workers_msg[128];
        char tasks_msg[128];
        snprintf(pool_msg, static_cast <size_t>(sizeof(pool_msg)), " ~ ThreadPool:%s", classify_pool.name());
        snprintf(workers_msg, static_cast <size_t>(sizeof(workers_msg)), "Workers[Busy:%u, Idle:%u, Exited:%u, Assignable:%u, Total:%u]",
            static_cast <unsigned long>(busy_worker), 
            static_cast <unsigned long>(idle_worker), 
            static_cast <unsigned long>(exited_worker), 
            static_cast <unsigned long>(assignable_worker), 
            static_cast <unsigned long>(total_worker));
        snprintf(tasks_msg, static_cast<size_t>(sizeof(tasks_msg)), "Tasks[Running:%lu, Waiting:%lu, Completed:%lu, Total:%lu]",
            static_cast <unsigned long>(running_task), 
            static_cast <unsigned long>(pending_task), 
            static_cast <unsigned long>(completed_task), 
            static_cast <unsigned long>(total_task));

        pool_msgs_.emplace_back(pool_msg);
        workers_msgs_.emplace_back(workers_msg);
        tasks_msgs_.emplace_back(tasks_msg);
    }

    std::thread t_;
    std::vector<std::string> pool_msgs_;
    std::vector<std::string> workers_msgs_;
    std::vector<std::string> tasks_msgs_;
};

class SmartThreadPoolBuilder {
public:
    SmartThreadPoolBuilder()
        : smart_pool_(new SmartThreadPool), enable_monitor_(false), monitor_second_period_(std::chrono::seconds(60)) {
    }

    SmartThreadPoolBuilder(SmartThreadPoolBuilder&&) = delete;
    SmartThreadPoolBuilder(const SmartThreadPoolBuilder&) = delete;
    SmartThreadPoolBuilder& operator=(SmartThreadPoolBuilder&&) = delete;
    SmartThreadPoolBuilder& operator=(const SmartThreadPoolBuilder&) = delete;

    SmartThreadPoolBuilder& AddClassifyPool(const char* pool_name, uint8_t capacity, uint8_t init_size) {
        auto pool = new ClassifyThreadPool(pool_name, capacity);
        pool->InitWorkers(init_size);
        smart_pool_->pools_.emplace(pool_name, pool);
        return *this;
    }
    SmartThreadPoolBuilder& EnableMonitor(const std::chrono::duration<int>& second_period = std::chrono::seconds(60)) {
        enable_monitor_ = true;
        monitor_second_period_ = second_period;
        return *this;
    }
    std::unique_ptr<SmartThreadPool> BuildAndInit() {
        if (enable_monitor_) {
            auto monitor = new Monitor();
            monitor->StartMonitoring(*smart_pool_.get(), monitor_second_period_);
        }
        return std::move(smart_pool_);
    }

private:
    std::unique_ptr<SmartThreadPool> smart_pool_;
    bool enable_monitor_;
    std::chrono::duration<int> monitor_second_period_;
};
