#include "stdafx.h"

#include <thread>

#include "BitFunnel/AsyncTask.h"
#include "BitFunnel/BitFunnelErrors.h"
#include "BitFunnel/PrioritizedTaskQueues.h"
#include "LoggerInterfaces/Logging.h"


namespace BitFunnel
{
    PrioritizedTaskQueues::PrioritizedTaskSchedulingData::PrioritizedTaskSchedulingData()
        : m_taskConfig(PrioritizedTaskConfig::TypeCount, 0, 0),
          m_currentConsumedThreadCount(0),
          m_queuedTaskCount(0)
    {
    }


    PrioritizedTaskQueues::PrioritizedTaskSchedulingData::PrioritizedTaskSchedulingData(PrioritizedTaskConfig const & config)
        : m_taskConfig(config),
          m_currentConsumedThreadCount(0),
          m_queuedTaskCount(0)
    {
        EvaluateTaskRunValidity();
    }


    void PrioritizedTaskQueues::PrioritizedTaskSchedulingData::EvaluateTaskRunValidity()
    {
        m_isAtPriorityToRun = (m_queuedTaskCount > 0 && m_currentConsumedThreadCount <= m_taskConfig.GetPriorityGrantingThreshold());
        m_isLegalToRun = (m_queuedTaskCount > 0 && m_currentConsumedThreadCount < m_taskConfig.GetMaxThreadCount());
    }

    unsigned __int32 PrioritizedTaskQueues::PrioritizedTaskSchedulingData::GetCurrentConsumedThreadCount() const
    {
        return m_currentConsumedThreadCount;
    }


    void PrioritizedTaskQueues::PrioritizedTaskSchedulingData::ConsumeThread()
    {
        LogAssertB(m_queuedTaskCount > 0);

        m_currentConsumedThreadCount++;
        m_queuedTaskCount--;
        EvaluateTaskRunValidity();
    }


    void PrioritizedTaskQueues::PrioritizedTaskSchedulingData::ReturnThread()
    {
        LogAssertB(m_currentConsumedThreadCount > 0);

        m_currentConsumedThreadCount--;
        EvaluateTaskRunValidity();
    }


    void PrioritizedTaskQueues::PrioritizedTaskSchedulingData::PostTask()
    {
        m_queuedTaskCount++;
        EvaluateTaskRunValidity();
    }


    bool PrioritizedTaskQueues::PrioritizedTaskSchedulingData::HasTasks() const
    {
        return m_queuedTaskCount > 0;
    }


    bool PrioritizedTaskQueues::PrioritizedTaskSchedulingData::IsLegalToRun() const
    {
        return m_isLegalToRun;
    }


    bool PrioritizedTaskQueues::PrioritizedTaskSchedulingData::IsAtPriorityToRun() const
    {
        return m_isAtPriorityToRun;
    }


    PrioritizedTaskQueues::Mutex::Mutex()
    {
        InitializeCriticalSection(&m_criticalSection);
    }


    PrioritizedTaskQueues::Mutex::Mutex(unsigned __int32 spinCount)
    {
        InitializeCriticalSectionAndSpinCount(&m_criticalSection, spinCount);
    }


    PrioritizedTaskQueues::Mutex::Mutex(unsigned __int32 spinCount, unsigned __int32 flags)
    {
        InitializeCriticalSectionEx(&m_criticalSection, spinCount, flags);
    }


    PrioritizedTaskQueues::Mutex::~Mutex()
    {
        DeleteCriticalSection(&m_criticalSection);
    }


    void PrioritizedTaskQueues::Mutex::Lock()
    {
        EnterCriticalSection(&m_criticalSection);
    }


    bool PrioritizedTaskQueues::Mutex::TryLock()
    {
        return !!TryEnterCriticalSection(&m_criticalSection);
    }


    void PrioritizedTaskQueues::Mutex::Unlock()
    {
        LeaveCriticalSection(&m_criticalSection);
    }


    PrioritizedTaskQueues::LockGuard::LockGuard(Mutex& mutex)
        : m_mutex(mutex)
    {   
        m_mutex.Lock();
    }


    PrioritizedTaskQueues::LockGuard::~LockGuard()
    {
        m_mutex.Unlock();
    }


    bool IsPrioritizedTaskConfigValid(std::vector<PrioritizedTaskConfig> const & configList,
                                      unsigned __int32 totalThreadCount)
    {
        if (configList.size() != PrioritizedTaskConfig::TypeCount)
        {
            return false;
        }

        for (unsigned i = 0; i < configList.size(); ++i)
        {
            PrioritizedTaskConfig const & config = configList[i];

            // The config must be in the order of the enum value defined in PrioritizedTaskConfig::Type.
            if (static_cast<unsigned>(config.GetType()) != i)
            {
                return false;
            }

            if (config.GetMaxThreadCount() > totalThreadCount)
            {
                return false;
            }
        }

        return true;
    }


    PrioritizedTaskQueues::PrioritizedTaskQueues(std::vector<PrioritizedTaskConfig> const & configList,
                                                 unsigned __int32 totalThreadCount,
                                                 unsigned __int32 concurrentThreadCount)
        : m_availableThreadCount(totalThreadCount),
          m_totalThreadCount(totalThreadCount)
    {
        // Validate the list of configurations.
        if (concurrentThreadCount > totalThreadCount)
        {
            throw BitFunnelError("Number of concurrent thread should not be greater than the total thread count.");
        }

        if (!IsPrioritizedTaskConfigValid(configList, m_totalThreadCount))
        {
            throw BitFunnelError("Invalid PrioritizedTaskConfig list.");
        }

        for (unsigned i = 0; i < configList.size(); ++i)
        {
            m_prioritizedTaskSchedulingDataList[i] = configList[i];
        }

        // Create IOCompletionPorts.
        for (unsigned i = 0; i < PrioritizedTaskConfig::TypeCount; ++i)
        {
            m_prioritizedQueueHandles[i] = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE,
                                                                    NULL,
                                                                    NULL,
                                                                    concurrentThreadCount);

            LogAssertB(m_prioritizedQueueHandles[i] != NULL);
        }
    }


    PrioritizedTaskQueues::~PrioritizedTaskQueues()
    {
        for (unsigned i = 0; i < PrioritizedTaskConfig::TypeCount; ++i)
        {
            CloseHandle(m_prioritizedQueueHandles[i]);
        }
    }


    bool PrioritizedTaskQueues::TryGetTask(bool isExitMode, unsigned& taskType)
    {
        LockGuard lock(m_lock);

        if (m_availableThreadCount == 0)
        {
            return false;
        }

        for (uint32_t iterationCnt = 0; iterationCnt < PrioritizedTaskConfig::TypeCount; ++iterationCnt)
        {
            if (m_prioritizedTaskSchedulingDataList[iterationCnt].IsAtPriorityToRun())
            {
                taskType = iterationCnt;

                // Allocate thread for the next job.
                m_prioritizedTaskSchedulingDataList[taskType].ConsumeThread();
                m_availableThreadCount--;

                return true;
            }
        }

        // Second look for Task types that are legal to run.
        for (uint32_t iterationCnt = 0; iterationCnt < PrioritizedTaskConfig::TypeCount; ++iterationCnt)
        {
            if (m_prioritizedTaskSchedulingDataList[iterationCnt].IsLegalToRun())
            {
                taskType = iterationCnt;

                // Allocate thread for the next job.
                m_prioritizedTaskSchedulingDataList[taskType].ConsumeThread();
                m_availableThreadCount--;

                return true;
            }
        }

        // In the special case of shutdown, look for any Task type that still has work available.
        // Scan starts at first queue since priority doesn't matter in shutdown mode.
        if (isExitMode)
        {
            const unsigned size = PrioritizedTaskConfig::TypeCount;
            for (unsigned i = 0; i < size; ++i)
            {
                if (m_prioritizedTaskSchedulingDataList[i].HasTasks())
                {
                    taskType = i;

                    // Allocate thread for the next job.
                    m_prioritizedTaskSchedulingDataList[taskType].ConsumeThread();
                    m_availableThreadCount--;

                    return true;
                }
            }
        }

        return false;
    }


    AsyncTask* PrioritizedTaskQueues::GetNextTask(bool isExitMode)
    {
        unsigned nextJobType = 0;

        if (TryGetTask(isExitMode, nextJobType))
        {
            try
            {
                return PullTask(nextJobType);
            }
            catch (...)
            {
                NotifyTaskFinishInternal(static_cast<PrioritizedTaskConfig::Type>(nextJobType));
            }           
        }
        
        return reinterpret_cast<AsyncTask*>(nullptr);        
    }


    AsyncTask* PrioritizedTaskQueues::PullTask(unsigned taskType)
    {
        // Pull the job from the corresponding queue. This should be done outside of lock.
        DWORD bytes = 0;
        ULONG_PTR key = 0;
        LPOVERLAPPED overlapped = NULL;
        BOOL status = FALSE;

        status = GetQueuedCompletionStatus(m_prioritizedQueueHandles[taskType],
                                           &bytes,
                                           &key,
                                           &overlapped,
                                           0);

        LogAssertB(status == TRUE);
        LogAssertB(overlapped != nullptr);

        return static_cast<AsyncTask*>(overlapped);
    }


    void PrioritizedTaskQueues::NotifyTaskFinish(AsyncTask* taskFinished)
    {
        NotifyTaskFinishInternal(taskFinished->GetType());
    }


    void PrioritizedTaskQueues::NotifyTaskFinishInternal(PrioritizedTaskConfig::Type taskType)
    {
        LockGuard lock(m_lock);

        LogAssertB(m_availableThreadCount <= m_totalThreadCount);

        m_prioritizedTaskSchedulingDataList[taskType].ReturnThread();
        m_availableThreadCount++;
    }


    void PrioritizedTaskQueues::PostTask(AsyncTask* taskToPost)
    {
        const PrioritizedTaskConfig::Type type = taskToPost->GetType();
        const BOOL success =
            ::PostQueuedCompletionStatus(m_prioritizedQueueHandles[type],
                                         0,
                                         static_cast<DWORD>(0),
                                         static_cast<LPOVERLAPPED>(taskToPost));
         
        LogAssertB(success || GetLastError() == ERROR_IO_PENDING);

        LockGuard lock(m_lock);
        m_prioritizedTaskSchedulingDataList[type].PostTask();
    }


    bool PrioritizedTaskQueues::HasAnyTask()
    {
        LockGuard lock(m_lock);
        for (unsigned i = 0; i < PrioritizedTaskConfig::TypeCount; ++i)
        {
            if (m_prioritizedTaskSchedulingDataList[i].HasTasks())
            {
                return true;
            }
        }

        return false;
    }
}
