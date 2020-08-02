#pragma once

#include <memory>
#include <vector>

#include "BitFunnel/NonCopyable.h"
#include "BitFunnel/PrioritizedTaskConfig.h"


namespace BitFunnel
{
    class AsyncTask;

    //*************************************************************************
    //
    // PrioritizedTaskQueues manages multiple queues for different types of 
    // tasks and records the thread resource allocation situation for all types
    // of tasks managed by this class. 
    //
    // This class determines the next task which has the highest priority
    // to be scheduled and executed and gives that task to a thread pool for
    // execution.
    //
    // This class is thread safe.
    //
    //*************************************************************************
    class PrioritizedTaskQueues : private NonCopyable
    {
    public:
        PrioritizedTaskQueues(std::vector<PrioritizedTaskConfig> const & configList,
                              unsigned __int32 totalThreadCount,
                              unsigned __int32 concurrentThreadCount);
     
        ~PrioritizedTaskQueues();

        // Determine the next task to be executed and returns it to the caller.
        // If there is no task can be executed, a nullptr is returned.
        // The isExitMode indicates if the system is in exit mode.
        AsyncTask* GetNextTask(bool isExitMode);

        // Notify the PrioritizedTaskQueues that a particular task is finished so that
        // this class can adjust the resource allocation situation to reflect this
        // change. 
        void NotifyTaskFinish(AsyncTask* taskFinished);

        // Post a task to the PrioritizedTaskQueues.
        void PostTask(AsyncTask* taskToPost);

        // Check if there is any task left on any of the queues.
        bool HasAnyTask();

    private:

        // A helper class which records the thread resource allocation for a particular
        // type of task.
        //
        // DESIGN NOTE: this class is not thread safe. The caller of this class needs to
        // maintain thread safety.
        class PrioritizedTaskSchedulingData
        {
        public:
            PrioritizedTaskSchedulingData();

            PrioritizedTaskSchedulingData(PrioritizedTaskConfig const & config);

            // Get the number of threads which are currently working on
            // a task represented by the underlying PrioritizedTaskConfig.
            unsigned __int32 GetCurrentConsumedThreadCount() const;

            // Consume one thread for a task represented by the underlying PrioritizedTaskConfig.
            void ConsumeThread();

            // Return one thread for a task represented by the underlying PrioritizedTaskConfig.
            void ReturnThread();

            // Post a task represented by the underlying PrioritizedTaskConfig.
            void PostTask();

            // Check if there are any task represented by the underlying PrioritizedTaskConfig.
            bool HasTasks() const;

            // Check if the type of task is legal to run based on the config.
            bool IsLegalToRun() const;

            // Check if the type of task is at a higher priority to be scheduled to run based on the config.
            bool IsAtPriorityToRun() const;

        private:
            // Helper function to evaluate the IsLegalToRun and IsAtPriorityToRun condition.
            void EvaluateTaskRunValidity();

            // The underlying priority config
            PrioritizedTaskConfig m_taskConfig;

            // The number of threads have been allocated to the task.
            unsigned __int32 m_currentConsumedThreadCount;

            // The number of tasks in queue.
            unsigned __int32 m_queuedTaskCount;

            // Flag indicates if the task is legal to run.
            bool m_isLegalToRun;

            // Flag indicates if the task is at a higher priority to be scheduled to run.
            bool m_isAtPriorityToRun;
        };


        //*************************************************************************
        //
        // Mutex is a C++ class wrapper for the Win32 CRITICAL_SECTION structure.
        //
        //*************************************************************************
        class Mutex : NonCopyable
        {
        public:
            // Construct a mutex.
            Mutex();

            // Construct a mutex, initializing the critical section with a
            // specified spin count. See Win32 documentation on CRITICAL_SECTION
            // for more information.
            Mutex(unsigned __int32 spinCount);

            // Construct a mutex, initializing the critical section with a
            // specified spin count and flags. See Win32 documentation on
            // CRITICAL_SECTION for more information.
            Mutex(unsigned __int32 spinCount, unsigned __int32 flags);

            // Destroys the mutex.
            ~Mutex();

            // Locks the critical section inside the mutex. This method will
            // block until the lock is successfully acquired.
            void Lock();

            // Attempts to lock the critical section inside the mutex without
            // blocking. Returns true if the critical section was successfully
            // locked. Otherwise returns false.
            bool TryLock();

            // Unlocks the critical section inside the mutex.
            void Unlock();

        private:
            CRITICAL_SECTION m_criticalSection;
        };


        //*************************************************************************
        //
        // LockGuard is an RAII helper class that locks a mutex on construction and
        // unlocks the mutex on destruction. 
        //
        //*************************************************************************
        class LockGuard : NonCopyable
        {
        public:
            // Construct a LockGuard that immediately locks the Mutex parameter.
            // This will block until the lock has been acquired.
            explicit LockGuard(Mutex& lock);

            // Unlocks the mutex and destroys the LockGuard.
            ~LockGuard();

        private:
            Mutex& m_mutex;
        };


        // Helper function to try to get the next to run task.
        bool TryGetTask(bool isExitMode, unsigned& taskType);

        // Helper function to pull a task from a specific task queue.
        AsyncTask* PullTask(unsigned taskType);

        // Helper function to notify the PrioritizedTaskQueues that a task of a 
        // particular type is finished.
        void NotifyTaskFinishInternal(PrioritizedTaskConfig::Type taskType);

        // The list of scheduling data for different type of tasks.
        PrioritizedTaskSchedulingData m_prioritizedTaskSchedulingDataList[PrioritizedTaskConfig::TypeCount];

        // The list of priority queues.
        HANDLE m_prioritizedQueueHandles[PrioritizedTaskConfig::TypeCount];

        // Total number of threads (total resources).
        const unsigned __int32 m_totalThreadCount;

        // Lock protecting m_availableThreadCount.
        Mutex m_lock;

        // Available resources.
        unsigned __int32 m_availableThreadCount;
    };
}
