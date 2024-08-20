/*
 * SPDX-FileCopyrightText: 2024 Duncan Greatwood
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/******************************************************************************
 * pist_timelog.cpp
 *
 * Utility to log start and end time of activities
 *
 */

#include <pistache/pist_timelog.h>

#ifdef _IS_WINDOWS
#include <windows.h> // required for PST_THREAD_HDR (processthreadsapi.h)
#endif
#include PIST_QUOTE(PST_THREAD_HDR) //e.g. pthread.h

#ifdef DEBUG
unsigned __PS_TIMEDBG::mUniCounter = 0;

std::map<PST_THREAD_ID, unsigned> __PS_TIMEDBG::mThreadMap;
std::mutex __PS_TIMEDBG::mThreadMapMutex;

/* ------------------------------------------------------------------------- */

// returns depth value after increment
unsigned __PS_TIMEDBG::getThreadNextDepth() 
{
    std::lock_guard<std::mutex> l_guard(mThreadMapMutex);
    PST_THREAD_ID pthread_id = PST_THREAD_ID_SELF();
            
    std::map<PST_THREAD_ID, unsigned>::iterator it =
        mThreadMap.find(pthread_id);
    if (it == mThreadMap.end())
    {
        std::pair<PST_THREAD_ID, unsigned> pr(pthread_id, 1);
        mThreadMap.insert(pr);
        return(1);
    }

    return(++(it->second));
}

/* ------------------------------------------------------------------------- */

// returns depth value before decrement
unsigned __PS_TIMEDBG::decrementThreadDepth()
{
    std::lock_guard<std::mutex> l_guard(mThreadMapMutex);
    PST_THREAD_ID pthread_id = PST_THREAD_ID_SELF();
            
    std::map<PST_THREAD_ID, unsigned>::iterator it =
        mThreadMap.find(pthread_id);

    unsigned old_depth = 1;
    if (it->second) // else something went wrong
        old_depth = ((it->second)--);

    if (old_depth <= 1)
        mThreadMap.erase(it); // arguably optional, but avoids any risk
    // of leaks
    return(old_depth);
}



/* ------------------------------------------------------------------------- */

#endif
