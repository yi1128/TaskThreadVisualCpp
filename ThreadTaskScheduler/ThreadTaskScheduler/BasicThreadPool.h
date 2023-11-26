/*
    BasicThreadPool.h
    std::thread 기반 ThreadPool
*/

#pragma once

#include <atomic>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>

#define CPP11
//#define CPP17

template <typename T>
class CThreadQueue 
{
public:
    void tqPush(const T& t);
    bool tqPop(T* const t);

private:
    std::queue<T> m_queue;
    std::mutex m_mutex;
};

template <typename T>
void
CThreadQueue<T>::tqPush(const T& t) 
{
    //std::cerr << "std::queue push" << std::endl;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue.emplace(t);
}

template <typename T>
bool 
CThreadQueue<T>::tqPop(T* const t) 
{
    std::lock_guard<std::mutex> lock(m_mutex);
    //std::cerr << m_queue.size() << std::endl;
    if (m_queue.empty()) 
        return false;

    *t = m_queue.front();
    m_queue.pop();
    return true;
}

class CBasicThreadPool 
{
public:
    explicit CBasicThreadPool(int32_t thread_count);
    ~CBasicThreadPool();

    template <typename F, typename... Args>
    // C++ 17 에서부터는 result_of가 사용되지 않음
#ifdef CPP11
    auto push(F&& f, Args&&... args)
        ->std::future<typename std::result_of<F(Args...)>::type>;
#endif
#ifdef CPP17
    auto push(F&& f, Args&&... args)
        ->std::future<typename std::invoke_result<F(Args...)>::type>;
#endif

private:
    CThreadQueue<std::function<void()>> m_queue;
    std::vector<std::thread> m_vThreadList;
    std::atomic<bool> m_stop = { false };
};

CBasicThreadPool::CBasicThreadPool(int32_t _threadCount)
{
    for (int32_t i = 0; i < _threadCount; i++) 
    {
        m_vThreadList.emplace_back([this]() {
            while (!m_stop) 
            {
                std::function<void()> f;
                if (m_queue.tqPop(&f)) 
                {
                    f();
                    continue;
                }
                std::this_thread::yield();
            }
        });
    }
}

CBasicThreadPool::~CBasicThreadPool()
{
    m_stop.store(true);
    for (auto& e : m_vThreadList) 
    {
        if (e.joinable())
            e.join();
    }
}


template <typename F, typename... Args>
auto CBasicThreadPool::push(F&& f, Args&&... args)
-> std::future<typename std::result_of<F(Args...)>::type> 
{
    // C++ 17 에서부터는 result_of가 사용되지 않음
#ifdef CPP11
    using ret_type = typename std::result_of<F(Args...)>::type;
#endif
#ifdef CPP17
    using ret_type = typename std::invoke_result<F(Args...)>::type;
#endif
    auto exec = std::make_shared<std::packaged_task<ret_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...));

    std::function<void()> callback_f([exec]() {
        (*exec)();
        });
    m_queue.tqPush(callback_f);

    std::future<ret_type> ret = exec->get_future();
    return ret;
}

