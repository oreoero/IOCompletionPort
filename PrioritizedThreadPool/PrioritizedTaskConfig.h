#pragma once


namespace BitFunnel
{
    //*************************************************************************
    //
    // PrioritizedTaskConfig specifies a task scheduling schema which is used
    // to determine the scheduling priority of a task.
    //
    // A task can belong to one of the following types, which are High, Medium, 
    // and Low. Please note that the type of a task doesn't specify the priority
    // of the task. The priority of a task is always dynamically determined during
    // runtime. The scheduling schema of a task is specified by two variables, 
    // which are priorityGrantingThreshold and maxThreadCount.
    //
    // The priorityGrantingThreshold specifies a threshold which controls 
    // whether the task should be considered to be scheduled with higher priority.
    // The priorityGrantingThreshold says, when the total number of inflight
    // tasks of a particular type is lower than the value of priorityGrantingThreshold, 
    // the current task could be assigned a higher schedule priority. However, 
    // higher scheduling priority schedule doesn't guarantee the task will be 
    // executed immediately depending on the availability of all current running
    // threads.
    //
    // For example, a low type task is posted and the priorityGrantingThreshold
    // of the low priority task is 1. It means, at the time when a low type task
    // is posted, if there is less than 1 low type task inflight, when there is 
    // a thread available, this low type task will have a higher priority to be 
    // scheduled.
    //
    // The maxThreadCount specifies the maximum number of threads could be allocated
    // for a task of certain type to avoid starvation of other types of tasks.
    //
    // If multiple types of tasks have higher scheduling priority, a random one
    // is selected. If no type of task have higher scheduling priority, then a 
    // random one which is legal to run is selected.
    //
    //*************************************************************************
    class PrioritizedTaskConfig
    {
    public:
        enum Type
        {
            High = 0,
            Medium = 1,
            Low = 2,

            TypeCount = 3
        };

        PrioritizedTaskConfig(Type type,
                              unsigned __int32 priorityGrantingThreshold,
                              unsigned __int32 maxThreadCount);

        // Getter functions.
        unsigned __int32 GetPriorityGrantingThreshold() const;
        unsigned __int32 GetMaxThreadCount() const;
        Type GetType() const;

    private:
        Type m_type;
        unsigned __int32 m_priorityGrantingThreshold;
        unsigned __int32 m_maxThreadCount; 
    };
}