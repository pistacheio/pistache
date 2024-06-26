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

#ifdef DEBUG
unsigned __PS_TIMEDBG::mUniCounter = 0;

std::map<pthread_t, unsigned> __PS_TIMEDBG::mThreadMap;
std::mutex __PS_TIMEDBG::mThreadMapMutex;
#endif
