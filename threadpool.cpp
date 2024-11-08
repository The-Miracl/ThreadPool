#include "threadpool.h"
#include <functional>
#include <thread>
#include <iostream>
#include <thread>
const int TASK_MAX_THRESHHOLD = 1024;
const int THREAD_MAX_THRESHHOLD = 10;
const int THREAD_MAX_IDLE_TIME = 10; // 单位是s

// 线程池构造
ThreadPool::ThreadPool()
    : initThreadSize_(0), taskSize_(0), idleThreadSize_(0), curThreadSize_(0), taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD), threadSizeThreshHold_(THREAD_MAX_THRESHHOLD), poolMode_(PoolMode::MODE_FIXED), isPoolRunning_(false)
{
}

ThreadPool::~ThreadPool()
{
    isPoolRunning_ = false;

    // 等待线程池里面所有的线程返回就结束，线程池的线程有两种状态：阻塞&正在执行任务中
    std::unique_lock<std::mutex> lock(taskQueMtx_);
    notEmpty_.notify_all();
    exitCond_.wait(lock, [&]() -> bool
                   { return threads_.size() == 0; });
}

// 设置线程池工作模式
void ThreadPool::setMode(PoolMode mode)
{
    if (checkRunningState())
        return;
    poolMode_ = mode;
}

// 设置task任务队列上限阈值
void ThreadPool::setTaskQueMaxThreshHold_(int threshhold)
{
    if (checkRunningState())
        return;
    taskQueMaxThreshHold_ = threshhold;
}

// 设置线程池cached模式下，线程数量阈值
void ThreadPool::setThreadSizeThreshHold_(int threshhold)
{
    if (checkRunningState())
        return;
    if (poolMode_ == PoolMode::MODE_CACHED)
    {
        threadSizeThreshHold_ = threshhold;
    }
}

// 给线程池提交任务  用户调用该接口，传入任务对象，生产任务,智能指针
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
    // 获取锁
    std::unique_lock<std::mutex> lock(taskQueMtx_);

    // 线程的通信  等待任务队列有空余     wait（一直等待，知道程序起来）  wait for（加了一个时间参数，最多等x秒）   wait until（等到什么时候，等到下周一）的区别
    // 用户提交任务，最长不能阻塞超过1s，否则判断提交任务失败，返回
    if (!notFull_.wait_for(lock, std::chrono::seconds(1),
                           [&]() -> bool
                           { return taskQue_.size() < (size_t)taskQueMaxThreshHold_; }))
    {
        // 表示notFull_等待1s，条件依然没有满足，即任务队列依然为满的，任务提交失败
        std::cerr << "task queue is full, submit task fail." << std::endl;
        // return task->getResult(); // 返回错误 不能用这种方法，因为线程执行完task后，task对象就被析构掉了，这样result就没用了
        return Result(sp, false);
    }

    // 如果任务队列不满，有空余，把任务放入任务队列中
    taskQue_.emplace(sp);
    taskSize_++;

    // 因为新放了任务，任务队列肯定不空，在notEmpty_进行通知,赶快分配线程执行任务
    notEmpty_.notify_all();

    // cached模式。任务处理比较紧急，场景：小而快的任务；需要根据任务数量和空闲线程数量，判断是否需要创建新的线程？
    if (poolMode_ == PoolMode::MODE_CACHED && taskSize_ > idleThreadSize_ && curThreadSize_ < threadSizeThreshHold_)
    {
        // 创建新的线程对象
        std::cout << ">>> create new thread..." << std::endl;
        // 创建thread线程对象的时候，把线程函数给到thread线程对象，绑定器，线程函数绑定到this指针里
        auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
        int threadId = ptr->getId();
        threads_.emplace(threadId, std::move(ptr));
        // 启动线程
        threads_[threadId]->start();
        // 修改线程个数相关的变量
        curThreadSize_++;
        idleThreadSize_++;
    }

    // 返回任务的Result对象
    return Result(sp);
}

// 开启线程池
void ThreadPool::start(int initThreadSize)
{
    // 设置线程池的运行状态
    isPoolRunning_ = true;

    // 记录初始线程个数
    initThreadSize_ = initThreadSize;
    curThreadSize_ = initThreadSize;

    // 创建线程对象
    for (int i = 0; i < initThreadSize_; i++)
    {
        // 创建thread线程对象的时候，把线程函数给到thread线程对象，绑定器，线程函数绑定到this指针里
        auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
        int threadId = ptr->getId();
        threads_.emplace(threadId, std::move(ptr));
        // threads_.emplace_back(std::move(ptr));
    }

    // 启动所有线程   std::vector<Thread *> threads_;
    for (int i = 0; i < initThreadSize_; i++)
    {
        threads_[i]->start(); // 线程启动，需要执行一个线程函数
        idleThreadSize_++;    // 记录初始空闲线程的数量
    }
}

// 定义线程函数  线程池的所有线程从任务队列里面消费任务
void ThreadPool::threadFunc(int threadid) // 线程函数执行完返回，相应的线程也就结束了
{
    auto lastTime = std::chrono::high_resolution_clock().now();

    // 所有任务必须执行完成，线程池才可以回收所有线程资源
    for (;;)
    {
        std::shared_ptr<Task> task;
        {
            // 获取锁
            std::unique_lock<std::mutex> lock(taskQueMtx_);

            std::cout << "tid:" << std::this_thread::get_id() << "尝试获取任务..." << std::endl;

            // cached模式下，有可能已经创建很多线程，但是空闲时间超过60s的话
            // 应该将多余的线程，结束回收
            // 当前时间-上一次线程执行的时间>60s

            // 每1s返回一次  怎么区分超时返回？还是有任务待执行返回
            // 锁+双重判断
            while (taskQue_.size() == 0)
            {
                // 线程池要结束，回收线程资源
                if (!isPoolRunning_) // 所有任务执行完再析构，释放资源
                {
                    // 线程函数结束，线程也结束了
                    threads_.erase(threadid);
                    std::cout << "threadid:" << std::this_thread::get_id() << "exit!" << std::endl;
                    exitCond_.notify_all();
                    return;
                }

                if (poolMode_ == PoolMode::MODE_CACHED)
                {
                    // 条件变量，超时返回
                    if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1)))
                    {
                        auto now = std::chrono::high_resolution_clock().now();
                        auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
                        if (dur.count() >= THREAD_MAX_IDLE_TIME && curThreadSize_ > initThreadSize_)
                        {
                            // 开始回收当前线程
                            // 记录线程数量的相关变量的值修改
                            // 把线程对象从线程列表容器中删除  没有办法threadFunc无法匹配thread对象
                            // threadid=》thread 对象=》删除
                            threads_.erase(threadid);
                            curThreadSize_--;
                            idleThreadSize_--;

                            std::cout << "threadid:" << std::this_thread::get_id() << "exit!" << std::endl;
                            return;
                        }
                    }
                }
                else
                {
                    //  等待notEmpty条件
                    notEmpty_.wait(lock);
                }

                // 线程池要结束，回收线程资源
                // if (!isPoolRunning_)
                // {
                //     threads_.erase(threadid);
                //     std::cout << "threadid:" << std::this_thread::get_id() << "exit!" << std::endl;
                //     exitCond_.notify_all();
                //     return; // 结束线程函数就是结束当前线程
                // }
            }

            idleThreadSize_--;

            std::cout << "tid:" << std::this_thread::get_id() << "获取任务成功..." << std::endl;

            // 如果任务队列不空，那么就从任务队列中取一个任务出来
            task = taskQue_.front();
            taskQue_.pop();
            taskSize_--;

            // 如果依然有剩余任务，继续通知其他线程执行任务
            if (taskQue_.size() > 0)
            {
                notEmpty_.notify_all();
            }

            // 取完一个任务，得进行通知,通知可以继续提交生成任务
            notFull_.notify_all();

        } // 取到任务就应该释放锁，这个作用域，可以使其自动析构，然后释放锁

        // 当前线程负责执行这个任务
        if (task != nullptr)
        {
            // task->run();//执行任务，把任务的返回值setVal方法给到Result
            task->exec();
        }
        idleThreadSize_++;
        lastTime = std::chrono::high_resolution_clock().now(); // 更新线程执行完任务的时间
    }
}

bool ThreadPool::checkRunningState() const
{
    return isPoolRunning_;
}

//////////////////线程方法实现
int Thread::generateId_ = 0;

// 线程构造
Thread::Thread(ThreadFunc func)
    : func_(func),
      threadId_(generateId_++)
{
}
// 线程析构
Thread::~Thread()
{
}

// 启动线程
void Thread::start()
{
    // 创建一个线程来执行一个线程函数
    std::thread t(func_, threadId_); // 对于C++11来说，线程对象t和线程函数func_
    t.detach();                      // 设置分离线程，因为t是局部函数，会析构，但是线程函数不能析构，，否则程序会宕机，因此将线程函数和线程对象分离
}

// 获取线程id
int Thread::getId() const
{
    return threadId_;
}

//////////////////Task方法的实现
Task::Task()
    : result_(nullptr)
{
}

void Task::exec()
{
    if (result_ != nullptr)
    {
        result_->setVal(run()); // 这里发生多态调用
    }
}
void Task::setResult(Result *res)
{
    result_ = res;
}

//////////////////Result方法的实现
Result::Result(std::shared_ptr<Task> task, bool isValid) : isValid_(isValid), task_(task)
{
    task_->setResult(this);
}

Any Result::get() // 用户调用的
{
    if (!isValid_)
    {
        return "";
    }

    sem_.wait(); // task任务如果没有执行完，这里会阻塞用户的线程
    return std::move(any_);
}

void Result::setVal(Any any)
{
    // 存储task的返回值
    this->any_ = std::move(any);
    sem_.post(); // 已经获取了任务的返回值，增加信号量资源

    //
}