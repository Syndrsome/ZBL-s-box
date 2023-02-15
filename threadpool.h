#ifndef THREADPOOL_H
#define THREADPOOL_H

#include<pthread.h>
#include<list>
#include "locker.h"
#include<exception>
#include<stdio.h>
#include<semaphore.h>

using namespace std;

template<typename T>
class threadpool{
public:
    threadpool(int thread_number = 8, int max_requests = 10000);
    ~threadpool();
    bool append(T* request);

private:
    static void* worker(void *arg);
    void run();

private:
    //线程数量
    int m_thread_number;
    //线程池
    pthread_t *m_threads;
    //请求队列的大小
    int m_max_requests;
    //请求队列
    list<T*> m_workqueue;
    //互斥锁
    locker m_queuelocker;
    //信号量
    sem m_queuestate;
    //是否结束
    bool m_stop;

};

template<typename T>
threadpool<T>::threadpool(int thread_number, int max_requests):
    m_thread_number(thread_number), m_max_requests(max_requests),
    m_stop(false), m_threads(NULL){
        if(thread_number <=0 || max_requests <= 0){
           
            throw exception();
        }
        m_threads = new pthread_t[m_thread_number];
        if(!m_threads){
           
            throw exception();
        }

        //创建线程池中的线程
        for(int i=0; i<m_thread_number; i++){
            printf("create the %dth thread\n",i);
            if(pthread_create(m_threads +i, NULL, worker, this) != 0){
                delete []m_threads;
                
                throw exception();
            }
           if( pthread_detach(m_threads[i])){
                delete []m_threads;
                
                throw exception();
           }
        }

    }

template<typename T>
threadpool<T>::~threadpool(){
    delete[] m_threads;
    m_stop = true;
}

template<typename T>
bool threadpool<T>::append(T* request){
    m_queuelocker.lock();
    if(m_workqueue.size() > m_max_requests){
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestate.post();
    return true;
}

template<typename T>
void *threadpool<T>::worker(void* arg){
    threadpool * pool = (threadpool*) arg;
    pool->run();
    return pool;

}

template<typename T>
void threadpool<T>::run(){
    while(!m_stop){
        m_queuestate.wait();
        m_queuelocker.lock();
        if(m_workqueue.empty()){
            m_queuelocker.unlock();
            continue;
        }

        T* requst = m_workqueue.front();
        
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if(!requst){
            continue;
        }
        requst->process();

    }
}

#endif