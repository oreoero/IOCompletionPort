#include "stdafx.h"

#include <memory>
#include <stdlib.h>
#include <vector>

#include "BitFunnel/AsyncTask.h"
#include "BitFunnel/PrioritizedAsyncTask.h"
#include "BitFunnel/PrioritizedTaskQueues.h"
#include "BitFunnel/PrioritizedThreadPool.h"
#include "BitFunnel/ThreadsafeCounter.h"
#include "SuiteCpp/UnitTest.h"
#include "ThreadAction.h"

namespace BitFunnel
{
    namespace PrioritizedThreadPoolUnitTest
    {      
        // RecordingAsyncTask derives from AsyncTask and it records the Execute and the destruction of the
        // object.
        class RecordingAsyncTask : public AsyncTask
        {
        public:
            RecordingAsyncTask(ThreadsafeCounter32& executionCount,
                               ThreadsafeCounter32& destructCount);

            ~RecordingAsyncTask();

            // AsyncTask API.
            virtual void Execute();

        private:
            ThreadsafeCounter32& m_executionCount;
            ThreadsafeCounter32& m_destructCount;
        };


        RecordingAsyncTask::RecordingAsyncTask(ThreadsafeCounter32& executionCount,
                                               ThreadsafeCounter32& destructCount)
            : m_executionCount(executionCount),
              m_destructCount(destructCount)
        {

        }


        RecordingAsyncTask::~RecordingAsyncTask()
        {
            m_destructCount.ThreadsafeIncrement();
        }


        void RecordingAsyncTask::Execute()
        {
            m_executionCount.ThreadsafeIncrement();
        }


        void PrioritizedThreadPoolBasicTest(PrioritizedThreadPoolConfig threadPoolConfig)
        {
            static constexpr unsigned c_threadActionTimeoutInMS = 5000;

            constexpr unsigned __int32 c_totalThreadCount = 16;
            constexpr unsigned __int32 c_priorityGrantingThresholdForHigh = 10;
            constexpr unsigned __int32 c_priorityGrantingThresholdForMed = 4;
            constexpr unsigned __int32 c_priorityGrantingThresholdForLow = 1;
            constexpr unsigned __int32 c_maxThreadCountForHigh = 16;
            constexpr unsigned __int32 c_maxThreadCountForMedium = 12;
            constexpr unsigned __int32 c_maxThreadCountForLow = 1;

            std::vector<PrioritizedTaskConfig> configList;
            configList.push_back(PrioritizedTaskConfig(PrioritizedTaskConfig::High, c_priorityGrantingThresholdForHigh, c_maxThreadCountForHigh));
            configList.push_back(PrioritizedTaskConfig(PrioritizedTaskConfig::Medium, c_priorityGrantingThresholdForMed, c_maxThreadCountForMedium));
            configList.push_back(PrioritizedTaskConfig(PrioritizedTaskConfig::Low, c_priorityGrantingThresholdForLow, c_maxThreadCountForLow));

            constexpr unsigned c_taskPostingThreadCount = 16;
            constexpr unsigned c_actionCountPerThread = 5000;
            constexpr unsigned c_highTaskThreshold = 70;
            constexpr unsigned c_mediumTaskThreshold = 29;
            
            ThreadsafeCounter32 destructionCounter;
            ThreadsafeCounter32 taskSpecificExecutionCounters[static_cast<unsigned>(PrioritizedTaskConfig::TypeCount)];

            {
                PrioritizedThreadPool threadPool(configList,
                                                 threadPoolConfig,
                                                 c_totalThreadCount,
                                                 c_totalThreadCount);
            
                // Create a list of actions which internally post tasks.
                std::vector<std::unique_ptr<ThreadAction>> threads;
                constexpr unsigned c_seed = 123456789;
                std::srand(c_seed);

                for (unsigned i = 0; i < c_taskPostingThreadCount; ++i)
                {
                    const auto postTaskAction
                        = ([&]()
                    {
                        for (size_t index = 0; index < c_actionCountPerThread; ++index)
                        {
                            const unsigned rand = std::rand() % 100;

                            PrioritizedTaskConfig::Type type;
                            if (rand >= c_highTaskThreshold)
                            {
                                type = PrioritizedTaskConfig::High;
                            }
                            else if (rand >= c_mediumTaskThreshold)
                            {
                                type = PrioritizedTaskConfig::Medium;
                            }
                            else
                            {
                                type = PrioritizedTaskConfig::Low;
                            }

                            auto& typeSpecificExecutionCounter = taskSpecificExecutionCounters[static_cast<unsigned>(type)];

                            RecordingAsyncTask* task = new RecordingAsyncTask(typeSpecificExecutionCounter,
                                                                              destructionCounter);
                            task->SetType(type);
                            threadPool.Invoke(*task);
                        }
                    });

                    threads.push_back(std::unique_ptr<ThreadAction>(
                        new ThreadAction(postTaskAction)));
                }

                for (auto const & thread : threads)
                {
                    const bool threadFinished
                        = thread->WaitForCompletion(c_threadActionTimeoutInMS);

                    TestAssert(threadFinished);
                }
            }

            // Test that the expected number of task is executed and destructed.
            const unsigned expectedTotalActionCount = c_taskPostingThreadCount * c_actionCountPerThread;
  
            unsigned observedTotalActionCount = 0;
            // Sum up the number of tasks executed for each type and 
            // ensure that every task type had at least one task executed.
            for (auto& counter: taskSpecificExecutionCounters)
            {
                const auto executedTaskCount = counter.ThreadsafeGetValue();
                TestAssert(executedTaskCount != 0);

                observedTotalActionCount += executedTaskCount;
            }

            TestAssert(observedTotalActionCount == expectedTotalActionCount);
            TestAssert(destructionCounter.ThreadsafeGetValue() == expectedTotalActionCount);
        }


        
        TestCase(PrioritizedThreadPoolMultiThreadBasicTest)
        {
            PrioritizedThreadPoolBasicTest(PrioritizedThreadPoolConfig::DefaultCpuGroupOnly);

            PrioritizedThreadPoolBasicTest(PrioritizedThreadPoolConfig::AllCpuGroupsWithGreedyAllocation);

            PrioritizedThreadPoolBasicTest(PrioritizedThreadPoolConfig::AllCpuGroupsWithUniformAllocation);
        }


        // This test simulates the thread pool usage that leverages a single task type and runs with more
        // than 64 threads.
        void
        PrioritizedThreadPoolLargeMultiThreadTestInternal(PrioritizedThreadPoolConfig threadPoolConfig)
        {
            constexpr unsigned c_threadActionTimeoutInMS = 5000;

            constexpr unsigned __int32 c_totalThreadCount = 82;
            constexpr unsigned __int32 c_priorityGrantingThresholdForHigh = 80;
            constexpr unsigned __int32 c_priorityGrantingThresholdForMed = 1;
            constexpr unsigned __int32 c_priorityGrantingThresholdForLow = 1;
            constexpr unsigned __int32 c_maxThreadCountForHigh = 80;
            constexpr unsigned __int32 c_maxThreadCountForMedium = 1;
            constexpr unsigned __int32 c_maxThreadCountForLow = 1;

            std::vector<PrioritizedTaskConfig> configList = 
            {
                {PrioritizedTaskConfig::High,   c_priorityGrantingThresholdForHigh, c_maxThreadCountForHigh},
                {PrioritizedTaskConfig::Medium, c_priorityGrantingThresholdForMed,  c_maxThreadCountForMedium},
                {PrioritizedTaskConfig::Low,    c_priorityGrantingThresholdForLow,  c_maxThreadCountForLow}
            };

            constexpr unsigned c_taskPostingThreadCount = 32;
            constexpr unsigned c_actionCountPerThread = 5000;

            ThreadsafeCounter32 executionCounter;

            {
                PrioritizedThreadPool threadPool(configList,
                                                 threadPoolConfig,
                                                 c_totalThreadCount,
                                                 c_totalThreadCount);

                // Create a list of actions which internally post tasks.
                std::vector<std::unique_ptr<ThreadAction>> threads;

                for (unsigned i = 0; i < c_taskPostingThreadCount; ++i)
                {
                    const auto postTaskAction
                        = ([&]()
                    {
                        for (size_t index = 0; index < c_actionCountPerThread; ++index)
                        {
                            PrioritizedAsyncTask* task = new PrioritizedAsyncTask(
                                PrioritizedTaskConfig::High,
                                [&]() {
                                    executionCounter.ThreadsafeIncrement();
                                });

                            threadPool.Invoke(*task);
                        }
                    });

                    threads.push_back(std::unique_ptr<ThreadAction>(
                        new ThreadAction(postTaskAction)));
                }

                for (auto const & thread : threads)
                {
                    const bool threadFinished
                        = thread->WaitForCompletion(c_threadActionTimeoutInMS);
                    TestAssert(threadFinished);
                }
            }

            // Test that the expected number of task is executed and destructed.
            constexpr unsigned totalActionCount = c_taskPostingThreadCount * c_actionCountPerThread;

            TestAssert(executionCounter.ThreadsafeGetValue() == totalActionCount);
        }


        TestCase(PrioritizedThreadPoolLargeMultiThreadTest)
        {
            PrioritizedThreadPoolLargeMultiThreadTestInternal(PrioritizedThreadPoolConfig::AllCpuGroupsWithGreedyAllocation);

            PrioritizedThreadPoolLargeMultiThreadTestInternal(PrioritizedThreadPoolConfig::AllCpuGroupsWithUniformAllocation);
        }
    }
}
