#include "stdafx.h"

#include <algorithm>
#include <limits>
#include <type_traits>

#include "BitFunnel/AsyncTask.h"
#include "BitFunnel/BitFunnelErrors.h"
#include "BitFunnel/PrioritizedTaskQueues.h"
#include "BitFunnel/PrioritizedThreadPool.h"
#include "LoggerInterfaces/Logging.h"
#include "ThreadAllocationStrategy.h"


namespace
{
    using CPUGroupInfo = std::vector<DWORD>;

    CPUGroupInfo GetCpuGroups()
    {
        const WORD activeGroups = GetActiveProcessorGroupCount();
        CPUGroupInfo cpuGroupInfo(activeGroups);

        for (WORD activeGroupIndex = 0; activeGroupIndex < activeGroups; ++activeGroupIndex)
        {
            cpuGroupInfo[activeGroupIndex] = GetActiveProcessorCount(activeGroupIndex);
        }

        return cpuGroupInfo;
    }
}

namespace BitFunnel
{
    // During system exit, a thread waits this amount of time before fails.
    static const DWORD c_threadPoolExitsWaitTimeInMs = 20000;

    // A thread waits this amount of time in the main IOCompletionPort to pick
    // up a new task.
    static const DWORD c_mainIOCompletionPortTimeoutInMS = 100;

    PrioritizedThreadPool::PrioritizedThreadPool(std::vector<PrioritizedTaskConfig> const & taskConfigList, 
                                                 const PrioritizedThreadPoolConfig threadpoolConfig,
                                                 unsigned __int32 threadCount,
                                                 unsigned __int32 concurrentThreadCount /* = 0 */)
        : m_completionPort(NULL),
          m_taskQueues(taskConfigList, threadCount, concurrentThreadCount),
          m_isExiting(false),
          m_attachedHandleCount(0)
    {
        LogThrowAssert(threadCount >= concurrentThreadCount,
                       "The count of threads in the thread pool (%u) cannot exceed the number "
                       "of threads that can run concurrently (%u).",
                       threadCount,
                       concurrentThreadCount);

        if (threadpoolConfig != DefaultCpuGroupOnly)
        {
            LogThrowAssert(threadCount <= (std::numeric_limits<DWORD>::max)(),
                           "Invalid number of threads in PrioritizedThreadPool: %u",
                           threadCount);
        }
        else
        {
            LogThrowAssert(threadCount <= MAXIMUM_WAIT_OBJECTS,
                           "Cannot require more than 64 threads from PrioritizedThreadPool "
                           "running in the DefaultCpuGroupOnly config. Got: %u",
                           threadCount);
        }

        m_completionPort = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE,
                                                    NULL,
                                                    NULL,
                                                    concurrentThreadCount);

        LogThrowAssert(m_completionPort != NULL, "Failed to create IO completion port.");

        if (threadpoolConfig == DefaultCpuGroupOnly)
        {
            InitializeThreadsWithoutAffinity(threadCount);
        }
        else if (threadpoolConfig == AllCpuGroupsWithGreedyAllocation)
        {
            InitializeThreadsWithAffinity<GreedyThreadAllocationStrategy>(threadCount);
        }
        else if (threadpoolConfig == AllCpuGroupsWithUniformAllocation)
        {
            InitializeThreadsWithAffinity<RoundRobinThreadAllocationStrategy>(threadCount);
        }
        else
        {
            LogThrowAbort("Invalid prioritized thread pool configuration.");
        }
    }


    void PrioritizedThreadPool::InitializeThreadsWithoutAffinity(unsigned __int32 threadCount)
    {
        // Initialize worker threads.
        m_threads.reserve(threadCount);

        for (unsigned __int32 i = 0; i < threadCount; i++)
        {
            m_threads.push_back(CreateWorkerThread());
        }
    }


    template<typename ThreadAllocationStrategy>
    void PrioritizedThreadPool::InitializeThreadsWithAffinity(unsigned __int32 desiredThreadCount)
    {
        static_assert(std::is_base_of<IThreadAllocationStrategy, ThreadAllocationStrategy>::value,
                      "Thread allocator must implement IThreadAllocator interface.");

        ThreadAllocationStrategy threadAllocator;

        // Initialize worker threads, using as many processor groups as possible.
        m_threads.reserve(desiredThreadCount);

        const auto cpuGroupInfo = GetCpuGroups();

        // Ensure that the list of CPU groups was loaded.
        LogThrowAssert(cpuGroupInfo.size() > 0, "Failed to get CPU groups information.");

        // Ensure that information for each of the CPU groups was fetched.
        LogThrowAssert(std::all_of(cpuGroupInfo.cbegin(),
                                    cpuGroupInfo.cend(),
                                    [] (CPUGroupInfo::value_type i) { return i != 0; }),
                       "Failed to fetch CPU count for at least one CPU group.");

        threadAllocator.CreateThreads(desiredThreadCount,
                                      cpuGroupInfo,
                                      [&] (WORD cpuGroup, size_t affinityMask)
        {
            GROUP_AFFINITY desiredGroupAffinity = { 0 };
            desiredGroupAffinity.Group = cpuGroup;
            desiredGroupAffinity.Mask = affinityMask;

            const HANDLE threadHandle = CreateWorkerThread();

            GROUP_AFFINITY oldGroupAffinity;
            SetThreadGroupAffinity(threadHandle, &desiredGroupAffinity, &oldGroupAffinity);

            m_threads.push_back(threadHandle);
        });
    }


    PrioritizedThreadPool::~PrioritizedThreadPool()
    {
        m_isExiting = true;

        for (unsigned __int32 i = 0; i < m_threads.size(); i++)
        {
            // Post an empty task so that the work threads can exit.
            PostTaskInternal(reinterpret_cast<AsyncTask*>(nullptr));
        }

        // Wait for all worker threads to exit, in batches of at most 64.
        for (size_t numThreadsRemaining = m_threads.size(); numThreadsRemaining > 0;)
        {
            constexpr DWORD maxThreadsPerWait = 64;
            const DWORD numThreadsToWaitFor = std::min<DWORD>(maxThreadsPerWait,
                                                              static_cast<DWORD>(numThreadsRemaining));

            const DWORD result = WaitForMultipleObjects(numThreadsToWaitFor, 
                                                        m_threads.data() + (m_threads.size() - numThreadsRemaining), 
                                                        true, 
                                                        c_threadPoolExitsWaitTimeInMs);

            LogAssertB(result == WAIT_OBJECT_0);

            numThreadsRemaining -= numThreadsToWaitFor;
        }

        // Make sure all attached handles are detached.
        LogAssertB(m_attachedHandleCount == 0);

        // Make sure to close all worker thread handles.
        for (auto& threadHandle : m_threads)
        {
            CloseHandle(threadHandle);
        }
        m_threads.clear();

        CloseHandle(m_completionPort);
    }


    void PrioritizedThreadPool::Invoke(AsyncTask& task)
    {
        if (m_isExiting)
        {
            return;
        }
        
        PostTaskInternal(&task);
    }


    void PrioritizedThreadPool::PostTaskInternal(AsyncTask* task)
    {
        const BOOL success =
            ::PostQueuedCompletionStatus(m_completionPort,
                                         0,
                                         static_cast<DWORD>(0),
                                         static_cast<LPOVERLAPPED>(task));

        LogAssertB(success || GetLastError() == ERROR_IO_PENDING);
    }


    HANDLE PrioritizedThreadPool::CreateWorkerThread() 
    {
        DWORD threadId = 0;
        HANDLE threadhandle = CreateThread(0,
                                           0,
                                           reinterpret_cast<LPTHREAD_START_ROUTINE>(PrioritizedThreadPool::Run),
                                           static_cast<LPVOID>(this),
                                           0,
                                           &threadId);

        return threadhandle;
    }


    void PrioritizedThreadPool::FinishTask(PrioritizedThreadPool* threadPool,
                                           AsyncTask* task)
    {
        threadPool->m_taskQueues.NotifyTaskFinish(task);

        // DESIGN NOTE: AsyncTask is a simple wrapper of a Windows OVERLAPPED data structure.
        // Since it needs to be passed throught IOCompletionPort, row pointer must be used, 
        // and it should always fits in the low fragmentation heap. As a result, calling delete
        // here should not have any perf issues.
        delete task;
    }


    void PrioritizedThreadPool::ProcessNextTask(PrioritizedThreadPool* threadPool,
                                                bool isLocalThreadInExitMode)
    {
        AsyncTask* nextTaskToRun = threadPool->m_taskQueues.GetNextTask(isLocalThreadInExitMode);        

        if (nextTaskToRun != nullptr)
        {
            try
            {
                nextTaskToRun->Execute();
            }
            catch (BitFunnelError& e)
            {
                FinishTask(threadPool, nextTaskToRun);
                throw e;
            }
            catch (std::exception& e)
            {
                FinishTask(threadPool, nextTaskToRun);
                throw e;
            }
            catch (...)
            {
                FinishTask(threadPool, nextTaskToRun);
                throw BitFunnelError("Unknown error during task execution.");
            }
            
            FinishTask(threadPool, nextTaskToRun);
        }        
    }


    DWORD PrioritizedThreadPool::Run(LPVOID data)
    {
        // A thread local flag to indicate if the thread is in exit mode.
        bool isLocalThreadInExitMode = false;

        PrioritizedThreadPool* threadPool = static_cast<PrioritizedThreadPool*>(data);

        for (;;)
        {
            DWORD bytes = 0;
            ULONG_PTR key = 0;
            LPOVERLAPPED overlapped = NULL;
            BOOL status = FALSE;

            ProcessNextTask(threadPool, isLocalThreadInExitMode);
                      
            // Then pickup task from the main IO completion port.
            status = GetQueuedCompletionStatus(threadPool->m_completionPort,
                                                &bytes,
                                                &key,
                                                &overlapped,
                                                c_mainIOCompletionPortTimeoutInMS);

            if (status == TRUE)
            {
                // Process the task from the main IO completion port.
                if (overlapped == nullptr)
                {
                    // A NULL task. This means the system is in exit mode.
                    isLocalThreadInExitMode = true;

                    if (threadPool->m_taskQueues.HasAnyTask())
                    {
                        // Still having unfinished tasks, post a NULL task to the PrioritizedTaskQueues so that
                        // thread can exit from there.
                        threadPool->PostTaskInternal(reinterpret_cast<AsyncTask*>(nullptr));
                    }
                    else
                    {
                        return 0;
                    }
                }
                else
                {
                    AsyncTask* asyncTask = static_cast<AsyncTask*>(overlapped);
                    threadPool->m_taskQueues.PostTask(asyncTask);
                }
            }    
        }
    }


    void PrioritizedThreadPool::Attach(HANDLE handle)
    {
        m_completionPort = CreateIoCompletionPort(handle, m_completionPort, 0, 0);
        m_attachedHandleCount++;

        LogAssertB(m_completionPort != NULL);
    }


    void PrioritizedThreadPool::Detach(HANDLE /* handle */)
    {
        m_attachedHandleCount--;
    }
}
