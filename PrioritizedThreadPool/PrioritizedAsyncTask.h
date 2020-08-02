#pragma once

#include <functional>

#include "BitFunnel/AsyncTask.h"
#include "BitFunnel/PrioritizedTaskConfig.h"

namespace BitFunnel
{
    //*************************************************************************
    //
    // The PrioritizedAsyncTask is an implementation of an AsyncTask that will
    // execute a function passed in parameter to the constructor. This allows
    // PrioritizedThreadPool users to leverage lambda's and boost::bind'ed
    // functions in their code.
    //
    //*************************************************************************
    class PrioritizedAsyncTask : public AsyncTask
    {
    public:
        // Creates a task of the given type and specifies the method to call on invocation.
        PrioritizedAsyncTask(PrioritizedTaskConfig::Type taskType,
                             std::function<void()> const & action)
            : m_action(action)
        {
            SetType(taskType);
        }

        // Executes the user-provided action.
        virtual void Execute() override
        {
            m_action();
        }

    private:
        // The function to execute.
        const std::function<void()> m_action;
    };
}

