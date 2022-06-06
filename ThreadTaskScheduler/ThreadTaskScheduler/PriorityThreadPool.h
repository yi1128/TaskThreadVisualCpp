#pragma once
/*
    PriorityThreadPool.h
    std::thread 기반 PriorityThreadPool
    BaseThreadPool.h 기반
    
    class 목록
    class ClassiftyThreadPool
    class PriorityThreadPool
    class Monitor - ThreadPool 모니터링용 Thread
    class PriorityThreadPoolBuilder - PriorityThreadPool을 만들기 위한 builder
*/

#include "BaseThreadPool.h"

namespace Detail 
{
    std::atomic<uint8_t> g_AutoIncrementThreadPool_ID = 0;
}

class ClassifyThreadPool 
{
public:
    ClassifyThreadPool(const char* name, uint16_t capacity)
        : m_id(++Detail::g_AutoIncrementThreadPool_ID), m_name(name), m_usCapacity(capacity) 
    {
        m_vWorkers.reserve(capacity);
        ConnectTaskPriorityQueue();
    }

    ClassifyThreadPool(ClassifyThreadPool&&) = delete;
    ClassifyThreadPool(const ClassifyThreadPool&) = delete;
    ClassifyThreadPool& operator=(ClassifyThreadPool&&) = delete;
    ClassifyThreadPool& operator=(const ClassifyThreadPool&) = delete;
    
    ~ClassifyThreadPool() { }

    void InitWorkers(uint16_t count) 
    {
        for (unsigned char i = 0; i < count && m_vWorkers.size() < m_usCapacity; i++) 
        {
            AddWorker();
        }
    }

    uint8_t id() const { return m_id; }
    uint16_t capacity() const { return m_usCapacity; }
    const char* GetName() const { return m_name.c_str(); }
    uint16_t GetWorkerCount() const { return static_cast<uint16_t>(m_vWorkers.size()); }
   
    uint16_t GetIdleWorkerCount() const 
    {
        return GetWorkerStateCount(Worker::State::IDLE);
    }

    uint16_t GetBusyWorkerCount() const 
    {
        return GetWorkerStateCount(Worker::State::BUSY);
    }

    uint16_t GetExitedWorkerCount() const 
    {
        return GetWorkerStateCount(Worker::State::EXITED);
    }

    const std::vector<Worker>& GetWorkers() const { return m_vWorkers; }
    const std::unique_ptr<TaskPriorityQueue>& GetTaskQueue() const { return m_taskQueue; }

private:
    friend class PriorityThreadPool;
    void ConnectTaskPriorityQueue() 
    {
        std::string queueName = m_name + "-->TaskQueue";
        m_taskQueue = std::unique_ptr<TaskPriorityQueue>{ new TaskPriorityQueue(queueName.c_str()) };
    }

    void AddWorker() 
    {
        m_vWorkers.emplace_back(m_taskQueue.get());
    }
    
    void ThreadJoinWorkers() 
    {
        for (auto& worker : m_vWorkers) 
        {
            worker.SetJoinWorker();
        }
    }

    uint16_t GetWorkerStateCount(Worker::State state) const 
    {
        uint16_t usCount = 0;
        for (auto& worker : m_vWorkers) 
        {
            if (worker.GetState() == state) 
            {
                usCount += 1;
            }
        }
        return usCount;
    }

    uint8_t m_id;
    std::string m_name;
    uint16_t m_usCapacity;
    std::vector<Worker> m_vWorkers;
    std::unique_ptr<TaskPriorityQueue> m_taskQueue;
};

class PriorityThreadPool 
{
public:
    PriorityThreadPool(PriorityThreadPool&&) = delete;
    PriorityThreadPool(const PriorityThreadPool&) = delete;
    PriorityThreadPool& operator=(PriorityThreadPool&&) = delete;
    PriorityThreadPool& operator=(const PriorityThreadPool&) = delete;
    
    // 소멸자
    ~PriorityThreadPool() { ThreadJoinAllWorkers(); }

    template<class F, class... Args>
    auto ApplyAsync(const char* pool_name, TaskPriority priority,
        F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type> 
    {
        auto& pool = m_pools.at(pool_name);
        auto res = pool->GetTaskQueue()->enqueue(priority, f, args...);
        if (pool->GetTaskQueue()->size() >= pool->GetWorkerCount()
            && pool->GetWorkerCount() < pool->capacity()) 
        {
            pool->AddWorker();
        }
        
        return res;
    }

    void ThreadJoinAllWorkers() 
    {
        for (auto&& pool : m_pools) 
        {
            pool.second->ThreadJoinWorkers();
        }
    }

private:
    friend class PriorityThreadPoolBuilder;
    friend class Monitor;
    PriorityThreadPool() {}
    std::map<std::string, std::unique_ptr<ClassifyThreadPool>> m_pools;
};

class Monitor 
{
public:
    void StartMonitoring(const PriorityThreadPool& pool, const std::chrono::duration<int>& monitor_second_period) 
    {
        m_thread = std::move(std::thread([&pool, &monitor_second_period, this]() 
            {
                while (true) 
                {
                    std::this_thread::sleep_for(monitor_second_period);
                    for (auto&& poolMap : pool.m_pools) 
                    {
                        auto& classifyPool = *poolMap.second.get();
                        MonitorClassifyPool(classifyPool);
                    }

                    char now[128];
                    std::time_t curTime = std::time(NULL);
                    struct tm tmCurTime;

                    errno_t e = localtime_s(&tmCurTime, &curTime);
                    if (e != false)
                        return;

                    std::strftime(now, sizeof(now), "%F %T", &tmCurTime);
                    std::string nowStr(now);

                    std::stringstream monitorLog;
                    auto cmp = [](const std::string& s1, const std::string& s2) { return s1.size() < s2.size(); };
                    size_t ulMaxRowMsgLength = 0;

                    for (size_t i = 0; i < m_vPoolMsgs.size(); ++i) 
                    {
                        int nMaxPoolMsgLength = std::max_element(m_vPoolMsgs.begin(), m_vPoolMsgs.end(), cmp)->length();
                        int nMaxWorkersMsgLength = std::max_element(m_vWorkersMsgs.begin(), m_vWorkersMsgs.end(), cmp)->length();
                        nMaxPoolMsgLength += 2;
                        nMaxWorkersMsgLength += 2;
                        std::stringstream rowLog;
                        rowLog << std::left << std::setw(nMaxPoolMsgLength) << m_vPoolMsgs.at(i)
                            << std::left << std::setw(nMaxWorkersMsgLength) << m_vWorkersMsgs.at(i)
                            << std::left << m_vTasksMsgs.at(i) << std::endl;

                        if (rowLog.str().length() > ulMaxRowMsgLength) 
                        {
                            ulMaxRowMsgLength = rowLog.str().length();
                        }
                        monitorLog << rowLog.str();
                    }

                    int nHeadFrontLength = (ulMaxRowMsgLength - nowStr.length()) / 2;
                    int nHeadBackLength = ulMaxRowMsgLength - nowStr.length() - nHeadFrontLength;
                
                    // 상태 메세지
                    std::stringstream prettyMsg;
                    prettyMsg << "/" << std::setfill('-') << std::setw(nHeadFrontLength)
                        << "" << now << std::setfill('-') << std::setw(nHeadBackLength)
                        << "\\" << std::endl
                        << monitorLog.str()
                        << "\\" << std::setfill('-') << std::setw(ulMaxRowMsgLength - 1)
                        << "/" << std::endl;
                
                    // 상태 메세지 출력
                    std::cerr << prettyMsg.str();

                    m_vPoolMsgs.clear();
                    m_vWorkersMsgs.clear();
                    m_vTasksMsgs.clear();
                }
            }));

        m_thread.detach();
    }

private:
    friend class PriorityThreadPoolBuilder;
    Monitor() {}
    void MonitorClassifyPool(const ClassifyThreadPool& classify_pool) {
        uint16_t usBusyWorker = classify_pool.GetBusyWorkerCount();
        uint16_t usIdleWorker = classify_pool.GetIdleWorkerCount();
        uint16_t usExitedWorker = classify_pool.GetExitedWorkerCount();
        uint16_t usTotalWorker = classify_pool.capacity();
        uint16_t usAssignableWorker = usTotalWorker - classify_pool.GetWorkerCount();

        uint64_t ulTotalTask = classify_pool.GetTaskQueue()->GetTaskCount();
        uint64_t ulRunningTask = classify_pool.GetBusyWorkerCount();
        uint64_t ulPendingTask = classify_pool.GetTaskQueue()->GetPendingTaskCount();
        uint64_t ulCompletedTask = ulTotalTask - ulRunningTask - ulPendingTask;

        char chPoolMsg[64];
        char chWorkersMsg[128];
        char chTasksMsg[128];

        snprintf(chPoolMsg, static_cast <size_t>(sizeof(chPoolMsg)), " ~ ThreadPool:%s", classify_pool.GetName());
        snprintf(chWorkersMsg, static_cast <size_t>(sizeof(chWorkersMsg)), "Workers[Busy:%u, Idle:%u, Exited:%u, Assignable:%u, Total:%u]",
            static_cast <unsigned long>(usBusyWorker), 
            static_cast <unsigned long>(usIdleWorker), 
            static_cast <unsigned long>(usExitedWorker), 
            static_cast <unsigned long>(usAssignableWorker), 
            static_cast <unsigned long>(usTotalWorker));
        snprintf(chTasksMsg, static_cast<size_t>(sizeof(chTasksMsg)), "Tasks[Running:%lu, Waiting:%lu, Completed:%lu, Total:%lu]",
            static_cast <unsigned long>(ulRunningTask), 
            static_cast <unsigned long>(ulPendingTask), 
            static_cast <unsigned long>(ulCompletedTask), 
            static_cast <unsigned long>(ulTotalTask));

        m_vPoolMsgs.emplace_back(chPoolMsg);
        m_vWorkersMsgs.emplace_back(chWorkersMsg);
        m_vTasksMsgs.emplace_back(chTasksMsg);
    }

    std::thread m_thread;
    std::vector<std::string> m_vPoolMsgs;
    std::vector<std::string> m_vWorkersMsgs;
    std::vector<std::string> m_vTasksMsgs;
};

class PriorityThreadPoolBuilder {
public:
    PriorityThreadPoolBuilder()
        : m_priorityPool(new PriorityThreadPool), m_bEnableMonitor(false), m_monitorSecondPeriod(std::chrono::seconds(60)) {
    }

    PriorityThreadPoolBuilder(PriorityThreadPoolBuilder&&) = delete;
    PriorityThreadPoolBuilder(const PriorityThreadPoolBuilder&) = delete;
    PriorityThreadPoolBuilder& operator=(PriorityThreadPoolBuilder&&) = delete;
    PriorityThreadPoolBuilder& operator=(const PriorityThreadPoolBuilder&) = delete;

    PriorityThreadPoolBuilder& AddClassifyPool(const char* pool_name, uint8_t capacity, uint8_t init_size) {
        auto pool = new ClassifyThreadPool(pool_name, capacity);
        pool->InitWorkers(init_size);
        m_priorityPool->m_pools.emplace(pool_name, pool);
        return *this;
    }
    PriorityThreadPoolBuilder& EnableMonitor(const std::chrono::duration<int>& second_period = std::chrono::seconds(60)) {
        m_bEnableMonitor = true;
        m_monitorSecondPeriod = second_period;
        return *this;
    }
    std::unique_ptr<PriorityThreadPool> BuildAndInit() 
    {
        if (m_bEnableMonitor) 
        {
            auto monitor = new Monitor();
            monitor->StartMonitoring(*m_priorityPool.get(), m_monitorSecondPeriod);
        }
        return std::move(m_priorityPool);
    }

private:
    std::unique_ptr<PriorityThreadPool> m_priorityPool;
    bool m_bEnableMonitor;
    std::chrono::duration<int> m_monitorSecondPeriod;
};
