#pragma once

#include <functional>
#include <vector>
#include <Windows.h>

namespace BitFunnel
{
    //*************************************************************************
    //
    // The IThreadAllocationStrategy interface describes an object which can
    // allocate a number of threads, given a CPU group configuration.
    //
    //*************************************************************************
    class IThreadAllocationStrategy
    {
    public:
        virtual ~IThreadAllocationStrategy() {}

        // This callback function will be invoked every time a thread needs to be created.
        using ThreadCreationCallback = std::function<void(WORD /* cpuGroup */, size_t /* affinityMask */)>;

        // Allocates a number of threads across the CPU groups.
        virtual void CreateThreads(unsigned desiredThreadCount,
                                   std::vector<DWORD> const & cpuCountPerGroup,
                                   ThreadCreationCallback const & threadCreationCallback) const = 0;
    };


    //*************************************************************************
    //
    // The GreedyThreadAllocationStrategy allocates threads as much as possible
    // to fill the first CPU group, then moves on to allocating threads to the
    // second CPU group, etc... until the desired number of threads is reached.
    // If there are more threads to allocate than the total number of
    // processors, oversubscription will occur and will allocate threads in the 
    // same greedy manner.
    //
    //*************************************************************************
    class GreedyThreadAllocationStrategy : IThreadAllocationStrategy
    {
    public:

        // Allocates a number of threads across the CPU groups.
        virtual void CreateThreads(unsigned desiredThreadCount,
                                   std::vector<DWORD> const & cpuCountPerGroup,
                                   ThreadCreationCallback const & threadCreationCallback) const override;
    };


    //*************************************************************************
    //
    // The RoundRobinThreadAllocator allocates one thread per CPU group, until
    // the desired number of threads is reached. If a CPU group has as many
    // threads allocated as it has processors, it will be skipped.
    //
    // If there are more threads to create than the total number of processors
    // across all CPU groups, oversubscription will occur. Threads will start
    // being allocated in a round-robin fashion, ignoring the group size.
    //
    //*************************************************************************
    class RoundRobinThreadAllocationStrategy : IThreadAllocationStrategy
    {
    public:

        // Allocates a number of threads across the CPU groups.
        virtual void CreateThreads(unsigned desiredThreadCount,
                                   std::vector<DWORD> const & cpuCountPerGroup,
                                   ThreadCreationCallback const & threadCreationCallback) const override;
    };
}

