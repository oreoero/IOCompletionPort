#include "stdafx.h"

#include "BitFunnel/BitFunnelErrors.h"
#include "BitFunnel/PrioritizedTaskConfig.h"


namespace BitFunnel
{
    PrioritizedTaskConfig::PrioritizedTaskConfig(Type type,
                                                 unsigned __int32 priorityGrantingThreshold,
                                                 unsigned __int32 maxThreadCount)
        : m_type(type),
          m_priorityGrantingThreshold(priorityGrantingThreshold),
          m_maxThreadCount(maxThreadCount)
    {
        if (m_priorityGrantingThreshold > m_maxThreadCount)
        {
            throw BitFunnelError("Invalid PrioritizedTaskConfig list.");
        }
    }


    unsigned __int32 PrioritizedTaskConfig::GetPriorityGrantingThreshold() const
    {
        return m_priorityGrantingThreshold;
    }


    PrioritizedTaskConfig::Type PrioritizedTaskConfig::GetType() const
    {
        return m_type;
    }


    unsigned __int32 PrioritizedTaskConfig::GetMaxThreadCount() const
    {
        return m_maxThreadCount;
    }
}