#include <iostream>
#include <thread>
#include <chrono>
#include "threadpool.h"

using namespace std;

/*
有些场景，希望获取线程的运行任务的返回值
举例：
计算1+...+30000
Thread1 1+...+10000
THread2 10001+...+20000
....

main Thread:给每一个线程分配计算的区间，并且等待他们算完返回结果，合并最终的结果即可
*/
using uLong = unsigned long long;
class MyTask : public Task
{
public:
    MyTask(int begin, int end)
        : begin_(begin),
          end_(end)
    {
    }

    // 问题1：怎么设计run函数的返回值，可以表示任意的类型
    // C++17 Any类型可以指向全部的类型
    Any run() // run方法最终就在线程池分配的线程中去做执行
    {
        std::cout << "tid:" << std::this_thread::get_id() << "begin!" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(3));
        uLong sum = 0;
        for (uLong i = begin_; i <= end_; i++)
        {
            sum += i;
        }
        std::cout << "tid:" << std::this_thread::get_id() << "end!" << std::endl;
        return sum;
    }

private:
    int begin_;
    int end_;
};

int main()
{
    {
        // 这块地方偶尔会出现死锁，即获取到任务的线程无法退出
        ThreadPool pool;
        // pool.setMode(PoolMode::MODE_CACHED);
        // 开始启动线程池
        pool.start(2); // 设置4个线程

        // linux上，这些Result对象也是局部对象，需要析构
        Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
        Result res2 = pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
        pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
        pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
        pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
        uLong sum1 = res1.get().cast_<uLong>();
        cout << sum1 << endl;
    } // Result对象也要析构！！！在vs下条件变量析构会释放相应的资源
    cout << "main over" << endl;
    getchar();
#if 0
    // 问题：ThreadPool对象析构以后，怎么把线程池相关的线程资源全部回收
    {
        ThreadPool pool;

        // 用户自己设置线程池模式
        pool.setMode(PoolMode::MODE_CACHED);

        // 开始启动线程池
        pool.start(4); // 设置4个线程

        // 任务队列
        // 问题2如何设计这里的Result机制
        Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
        Result res2 = pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
        Result res3 = pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
        pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));

        pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
        pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
        //
        uLong sum1 = res1.get().cast_<uLong>(); // 问题3.返回了一个Any类型，如何转成具体类型？
        uLong sum2 = res2.get().cast_<uLong>();
        uLong sum3 = res3.get().cast_<uLong>();

        // Master-Slave线程模型
        // Master线程（main）用来分解任务，然后给Slave线程分配任务
        // 等待各个slave线程执行完任务，返回结果
        // Master线程合并各个任务结果，输出
        cout << sum1 + sum2 + sum3 << endl;
    }
#endif

    return 0;
}