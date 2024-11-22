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
#include <atomic>

static std::atomic_uint gUniCounter = 0; // universal (static) counter

static std::map<PST_THREAD_ID, unsigned> gThreadMap;
static std::mutex gThreadMapMutex;

/* ------------------------------------------------------------------------- */

unsigned __PS_TIMEDBG::getNextUniCounter()
{
    return(++gUniCounter);
}

/* ------------------------------------------------------------------------- */

// returns depth value after increment
unsigned __PS_TIMEDBG::getThreadNextDepth()
{
    std::lock_guard<std::mutex> l_guard(gThreadMapMutex);
    PST_THREAD_ID pthread_id = PST_THREAD_ID_SELF();

    std::map<PST_THREAD_ID, unsigned>::iterator it =
        gThreadMap.find(pthread_id);
    if (it == gThreadMap.end())
    {
        std::pair<PST_THREAD_ID, unsigned> pr(pthread_id, 1);
        gThreadMap.insert(pr);
        return(1);
    }

    return(++(it->second));
}

/* ------------------------------------------------------------------------- */

// returns depth value before decrement
unsigned __PS_TIMEDBG::decrementThreadDepth()
{
    std::lock_guard<std::mutex> l_guard(gThreadMapMutex);
    PST_THREAD_ID pthread_id = PST_THREAD_ID_SELF();

    std::map<PST_THREAD_ID, unsigned>::iterator it =
        gThreadMap.find(pthread_id);

    unsigned old_depth = 1;
    if (it->second) // else something went wrong
        old_depth = ((it->second)--);

    if (old_depth <= 1)
        gThreadMap.erase(it); // arguably optional, but avoids any risk
    // of leaks
    return(old_depth);
}



/* ------------------------------------------------------------------------- */

#endif
