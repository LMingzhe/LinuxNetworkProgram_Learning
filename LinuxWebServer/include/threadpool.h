#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <iostream>
#include <pthread.h>
#include <list>
#include <exception>
#include "locker.h"

/**
 * @brief 线程池类
 * 
 */

template<typename T>
class threadpool
{
public:
    threadpool(int m_thread_number = 8, int m_max_request = 10000);
    bool append(T* request); // 添加请求到工作队列
    ~threadpool();

private:
    static void* worker(void* arg); // 线程执行的工作函数
    void run(); // 启动线程函数

private:
    int m_thread_number;   // 线程数量
    pthread_t* m_threads;  // 线程池数组，大小为 m_thread_number
    int m_max_request;     // 请求队列中最多允许的等待处理的请求数量
    std::list<T*> m_workqueue; // 请求队列/工作队列
    locker m_queuelocker;  // 互斥锁
    sem m_queuestate;      // 信号量，用来判读是否有人物需要处理
    bool m_stop;           // 是否结束线程
};

template<typename T>
threadpool<T>::threadpool(int thread_number, int max_request) : 
        m_thread_number(m_thread_number), m_max_request(m_max_request), m_stop(false),
        m_threads(NULL)
{
    if (thread_number <= 0 || max_request <= 0)
    {
        throw std::exception();
    }

    m_threads = new pthread_t[m_thread_number];
    if (!m_threads) // 创建失败，抛出异常
    {
        throw std::exception();
    }

    for (int i = 0; i < thread_number; ++i)
    {
        std::cout << "creat the " << i << "th thread" << std::endl;
        if (pthread_create(m_threads + i, NULL, worker, this) != 0) // 创建线程
        {
            delete[] m_threads;
            throw std::exception();
        }

        if (pthread_detach(m_threads[i])) // 分离线程
        {
            delete[] m_threads;
            throw std::exception();
        }
    }
}

template<typename T>
threadpool<T>::~threadpool()
{
    delete[] m_threads;
    m_stop = true;
}

template<typename T>
bool threadpool<T>::append(T* request)
{
    m_queuelocker.lock();

    if (m_workqueue.size() > m_max_request)
    {
        m_queuelocker.unlock();
        return false;
    }

    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestate.post(); // 增加信号量

    return true;
}

template<typename T>
void* threadpool<T>::worker(void* arg)
{
    threadpool* pool = (threadpool*) arg; // 当前线程池对象
    pool->run();
}

template<typename T>
void threadpool<T>::run()
{
    while (!m_stop)
    {
        m_queuestate.wait();
        m_queuelocker.lock();

        if (m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
        }

        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();

        if (!request)
        {
            continue;
        }

        request->process();

    }
}

#endif