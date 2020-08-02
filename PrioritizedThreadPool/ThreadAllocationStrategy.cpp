#include "stdafx.h"

#include "LoggerInterfaces/Logging.h"
#include "ThreadAllocationStrategy.h"


namespace
{
    // Given a number of CPUs in a CPU group, compute the affinity mask to give a thread
    // affinity to all CPUs in this group. For N CPUs, this will return all N lower order
    // bits set.
    size_t ComputeAffinityMask(DWORD cpuCount)
    {
        LogThrowAssert(cpuCount <= 64, "Cannot have more than 64 CPUs in a group.");
        return (UINT64_MAX >> (64 - cpuCount));
    }
}


namespace BitFunnel
{
    void
    GreedyThreadAllocationStrategy::CreateThreads(unsigned desiredThreadCount,
                                                  std::vector<DWORD> const & cpuCountPerGroup,
                                                  ThreadCreationCallback const & threadCreationCallback) const
    {
        unsigned allocatedThreadCount = 0;

        // Iterate through the processor groups, assigning a number of threads up to the processor
        // group size to each group until the maximum is reached.
        do
        {
            for (size_t cpuGroupIndex = 0;
                 cpuGroupIndex < cpuCountPerGroup.size() && allocatedThreadCount < desiredThreadCount;
                 ++cpuGroupIndex)
            {
                const size_t threadAffinity = ComputeAffinityMask(cpuCountPerGroup[cpuGroupIndex]);

                for (DWORD cpuIndex = 0;
                     cpuIndex < cpuCountPerGroup[cpuGroupIndex] && allocatedThreadCount < desiredThreadCount;
                     ++cpuIndex)
                {
                    threadCreationCallback(static_cast<WORD>(cpuGroupIndex), threadAffinity);

                    ++allocatedThreadCount;
                }
            }
        } while (allocatedThreadCount < desiredThreadCount);
    }


    void
    RoundRobinThreadAllocationStrategy::CreateThreads(unsigned desiredThreadCount,
                                                      std::vector<DWORD> const & cpuCountPerGroup,
                                                      ThreadCreationCallback const & threadCreationCallback) const
    {
        uint32_t totalCpuCount = 0;
        for (size_t cpuGroupIndex = 0; cpuGroupIndex < cpuCountPerGroup.size(); ++cpuGroupIndex)
        {
            totalCpuCount += cpuCountPerGroup[cpuGroupIndex];
        }

        std::vector<DWORD> assignedThreadCountPerCpuGroup(cpuCountPerGroup.size(), 0);

        uint32_t allocatedThreadCount = 0;
        uint32_t cpuGroupIndex = 0;

        // Allocate one thread at a time to each processor group, until the desired number of threads has been reached.
        do
        {
            // Set thread affinity to the current group if
            // - there are free CPUs in the group
            // - or all CPUs are already assigned (oversubscription)
            if (assignedThreadCountPerCpuGroup[cpuGroupIndex] < cpuCountPerGroup[cpuGroupIndex]
                || allocatedThreadCount >= totalCpuCount)
            {
                const size_t affinityMask = ComputeAffinityMask(cpuCountPerGroup[cpuGroupIndex]);

                threadCreationCallback(static_cast<WORD>(cpuGroupIndex), affinityMask);

                ++allocatedThreadCount;
                ++assignedThreadCountPerCpuGroup[cpuGroupIndex];
            }

            // Iterate through CPU groups.
            cpuGroupIndex = (cpuGroupIndex + 1) % cpuCountPerGroup.size();
        } while (allocatedThreadCount < desiredThreadCount);
    }
}

