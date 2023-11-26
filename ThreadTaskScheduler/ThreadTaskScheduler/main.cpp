#include <chrono>
#include <iostream>
#include <string>



#define BASIC_THREAD_POOL
//#define PRIORIRY_THREAD_POOL
//#define THREAD_SAFE_MAP

#ifdef BASIC_THREAD_POOL
#include "BasicThreadPool.h"

#include <chrono>
#include <numeric>
#include <execution>
/*
* 문제점
* BasicThreadPool 은 빠르지만 못돌리는 끝단쪽이 안돌아가고 있음 
* 대략 10000개 기준 500개의 task가 돌지않음
*/

using namespace std;

const int num_threads = 3;

void exec_f_g() {
    while (true) std::cout << "exec_f_g is called" << std::endl;
}

void exec_f_p(int param) {
    while (true) std::cout << "exec_f_p is called " << param << std::endl;
}

class T {
public:
    static void exec_f_l(T* t, std::string& s) {
        while (true) std::cout << "exec_f_l is called " << t->data_ << std::endl;
    }

    int32_t data_;
};

#define DATA_SIZE 10000
int t_count = 0;
int t_data[DATA_SIZE] = { 0, };
static std::mutex mtx_;
void func_void_int(int n) {
    bool prime = true;
    if (n < 2)
        prime = false;
    for (int i = 2; i < n; i++)
        if (n % i == 0)
            prime = false;
    {
        std::lock_guard<std::mutex> lg(mtx_);
        t_data[t_count++] = 1;
    }
    //cout << n << ": " << (prime ? "Prime" : "Composite") << endl;
}

void test_void_int() {
    CBasicThreadPool p(num_threads);
    vector<future<void>> f;
    int count = 0;
    for (int i = 0; i < DATA_SIZE; i++)
        p.push(func_void_int, i);
    /*
    for (auto i = f.begin(); i != f.end(); i++)
    {
        count++;
        i->get();
    }
    */
    //std::cout << count << std::endl;
}


int32_t main(int32_t argc, char** argv) {
    /*
    CBasicThreadPool tp(3);
    T t;
    t.data_ = 1314;
    std::string s = "exec_f_l";
    tp.push(&T::exec_f_l, &t, s);
    
    tp.push(exec_f_g);
    tp.push(exec_f_p, 2);
    */

    std::chrono::system_clock::time_point start = std::chrono::system_clock::now();
    test_void_int();
    std::chrono::system_clock::time_point end = std::chrono::system_clock::now();

    std::chrono::duration<double> time_span =
        std::chrono::duration_cast<std::chrono::duration<double>>(end - start);

    std::cout << "Algorithm Time span : " << time_span.count() << std::endl;

    // 스레드가 실행될 때까지의 대기 시간이 필요
    //std::this_thread::sleep_for(std::chrono::seconds(1));

    for (int i = 0; i < DATA_SIZE; i++)
    {
        if(t_data[i] != 1)
            cout << i << endl;
    }
    cout << "done" << endl;
    for (;;)
    {
        //std::this_thread::sleep_for(std::chrono::seconds(500));
        std::this_thread::yield();
    }
    return 0;
}

#endif

#ifdef PRIORIRY_THREAD_POOL
#include "PriorityThreadPool.h"

int main()
{
    // ********************How to init `SmartThreadPool`********************
    //
    // using stp::SmartThreadPool;
    // using stp::SmartThreadPoolBuilder;
    // SmartThreadPoolBuilder builder;

    // ********Build by calling a chain.********
    // builder.AddClassifyPool(const char* pool_name,
    //                         uint8_t capacity,
    //                         uint8_t init_size);
    // ******** Such as:
    // builder.AddClassifyPool("DefaultPool", 16, 4)
    //        .AddClassifyPool("CPUBoundPool", 8, 4)
    //        .AddClassifyPool("IOBoundPool", 16, 8)
    // auto pool = builder.BuildAndInit();  // will block current thread
    //
    // ***********************************************************************

    // ******************************How to join a task******************************
    //
    // pool->ApplyAsync(function, args...);
    // ******** Such as:
    // 1. Run a return careless task.
    // pool->ApplyAsync("IOBoundPool", TaskPriority::MEDIUM, [](){ //DoSomeThing(args...); }, arg1, arg2, ...);
    //
    // 2. Run a return careful task.
    // auto res = pool->ApplyAsync("CPUBoundPool", TaskPriority::HIGH, [](int count){ return count; }, 666);
    // auto value = res.get();
    //
    // or you can set a timeout duration to wait for the result to become available.
    //
    // std::future_status status = res.wait_for(std::chrono::seconds(1)); // wait for 1 second.
    // if (status == std::future_status::ready) {
    //   std::cout << "Result is: " << res.get() << std::endl;
    // } else {
    //   std::cout << "Timeout" << std::endl;
    // }
    //
    // *******************************************************************************
    
    PriorityThreadPoolBuilder builder;
    builder.AddClassifyPool("DefaultPool", 8, 4)
        .AddClassifyPool("CPUBoundPool", 8, 4)
        .AddClassifyPool("IOBoundPool", 64, 32)
        .EnableMonitor(std::chrono::seconds(1));
    auto pool = builder.BuildAndInit();

    for (int j = 0; j < 64; ++j) {
        for (unsigned char i = 0; i < 5; ++i) {
            
            pool->ApplyAsync("IOBoundPool", static_cast<TaskPriority>(i), [j](unsigned char i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                printf("%d\n", j);
                }, i);
            
            //pool->ApplyAsync("IOBoundPool", static_cast<TaskPriority>(i), exec_f_p, i);

        }
    }
    
    pool->ApplyAsync("IOBoundPool", TaskPriority::HIGH, []() {
        int repeat_times = 5;
        while (--repeat_times >= 0) {
            printf("IOBoundPool Task\n"); std::this_thread::sleep_for(std::chrono::seconds(2));
        }
        });
    
    auto res = pool->ApplyAsync("CPUBoundPool", TaskPriority::MEDIUM, [](int x, int y) {
        return x + y;
        }, 1, 2);
    
    auto value = res.get();
    printf("added result: %d\n", value);

    pool->ThreadJoinAllWorkers();
    
}
#endif

#ifdef THREAD_SAFE_MAP
#include "ThreadSafeMap.h"
struct dataStruct
{
    int x;
    int y;
    int z;
};
int main()
{
    int Key = 1;
    dataStruct n;
    n.x = 1;
    n.y = 2;
    n.z = 3;
    ThreadSafe::tsmap<int, dataStruct> _tsmap;
    _tsmap.emplace(std::pair<int, dataStruct>(Key, n));

}
#endif
