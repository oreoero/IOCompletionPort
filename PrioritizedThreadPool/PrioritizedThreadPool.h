#pragma once

#include <atomic>
#include <vector>
#include <Windows.h>


#include "BitFunnel/NonCopyable.h"
#include "BitFunnel/PrioritizedTaskConfig.h"
#include "BitFunnel/PrioritizedTaskQueues.h"


namespace BitFunnel
{
    class AsyncTask;

    enum PrioritizedThreadPoolConfig
    {
        // Allocate all threads inside the default CPU group assigned to the process.
        // There is a limit of 64 logical CPUs in a CPU group in Windows.
        // Requiring more threads than the number of CPUs available in the default
        // CPU group will lead to oversubscription.
        DefaultCpuGroupOnly,

        // Allocate threads and span CPU groups if needed. The greedy allocation method
        // will be used, which will do the following:
        //     1. Allocate threads to the first CPU group up to the number of CPUs it has.
        //     2. If there are still threads needing to be created, allocate threads
        //        to the second CPU group, up to the number of CPUs in that group.
        //     3. So on and so forth until the number of threads.
        //
        // If there are more threads to assign than the total number of CPUs across all
        // CPU groups, the algorithm will loop back to the first CPU group. This leads to
        // oversubscription.
        AllCpuGroupsWithGreedyAllocation,

        // Allocate threads and span CPU groups if needed. The uniform allocation method
        // will be used, which will evenly divide the requested number of threads across
        // all CPU groups. If the number of threads requested is greater than the total
        // number of CPUs across all CPU groups, this will lead to oversubscription.
        AllCpuGroupsWithUniformAllocation
    };

    //*************************************************************************
    //
    // PrioritizedThreadPool manages a pool of threads to excute tasks with
    // different priorities. The tasks with different priorities are controlled 
    // by the PrioritizedTaskQueues class which internally manages multiple
    // IOCompletion ports for each type of priority.
    //
    // The PrioritizedThreadPool uses IO completion port mechanism to keep track
    // of tasks posted by different sources. A client can attach a new source 
    // which could post task to the PrioritizedThreadPool. Similarly, a client
    // can also detach a source from the PrioritizedThreadPool.
    //
    // Internally, PrioritizedThreadPool has one IOCompletionPort for accepting
    // all tasks posted by clients. The tasks will be distributed to an instance
    // of PrioritizedTaskQueues which decides the scheduling priority of the tasks.
    //
    // The work flow of the PrioritizedThreadPool is as follows:
    // A thread trys to get the next task from the PrioritizedTaskQueues. The 
    // PrioritizedTaskQueues figures out the task which should have the highest 
    // priority to be scheduled. If there is no task there, the thread will go 
    // to the main IO completion port immediately and pull a task from it. 
    // Then the thread queues the task to the PrioritizedTaskQueues. That means,
    // the tasks in the PrioritizedTaskQueues always have higher priority to be 
    // scheduled than the tasks in the main IOCompletion port, since they are older.
    // And among the tasks in the PrioritizedTaskQueues, the scheduling priorities 
    // are determined dynamically by the current situation of the system.
    //
    // During system exiting, a list of NULL task (equal to the number of threads
    // in the thread pool) are posted to the main IO completion port. Once a 
    // thread picks up a NULL task from the main IO completion port, it marks
    // itself in the exiting mode, and queue the NULL task to the internal
    // PrioritizedTaskQueues. Thread will exit the system when it pickup a NULL
    // task from the PrioritizedTaskQueue.
    // 
    //*************************************************************************
    class PrioritizedThreadPool : private NonCopyable
    {

    public:
        // Zero number of concurrent threads will allow simultaneous 
        // execution of as many threads as many CPU/cores as possible based
        // on the thread pool configuration.
        // This is default value.  
        PrioritizedThreadPool(std::vector<PrioritizedTaskConfig> const & taskConfigList, 
                              const PrioritizedThreadPoolConfig threadpoolConfig,
                              unsigned __int32 threadCount,
                              unsigned __int32 concurrentThreadCount = 0);


        ~PrioritizedThreadPool();

        // Post a task to the thread pool.
        void Invoke(AsyncTask& task);

        // Attach a new source to which a task can be posted.
        void Attach(HANDLE handle);

        // Detach a source.
        void Detach(HANDLE handle);

    private:

        // This is the actual thread function, executed by the worker threads.
        static DWORD Run(LPVOID data);

        // Internal helper function to process a task, executed by the worker threads.
        static void ProcessNextTask(PrioritizedThreadPool* threadPool,
                                    bool isLocalThreadInExitMode);

        // Internal helper function to do clear up work after a task is done.
        static void FinishTask(PrioritizedThreadPool* threadPool,
                               AsyncTask* task);

    private:
        // Initializes a number of threads without any specific affinity.
        void InitializeThreadsWithoutAffinity(unsigned __int32 threadCount);

        // Initializes threads considering CPU group affinity, spanning processor groups if required.
        template<typename ThreadAllocationStrategy>
        void InitializeThreadsWithAffinity(unsigned __int32 threadCount);
        
        // Internal helper function to post a task which could be a nullptr.
        void PostTaskInternal(AsyncTask* task);

        // Creates a new thread that will execute the worker thread function.
        // The thread that gets created has no specific affinity.
        HANDLE CreateWorkerThread();

        // Main IO completion port to queue all the external tasks.
        HANDLE m_completionPort;

        // The underlying PrioritizedTaskQueues.
        PrioritizedTaskQueues m_taskQueues;

        // Collection of working threads.
        std::vector<HANDLE> m_threads;

        // The total number of attached handles.
        std::atomic<unsigned> m_attachedHandleCount;

        // Flag indicates if the system is exiting.
        std::atomic<bool> m_isExiting;
    };
}
