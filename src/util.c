//-----------------------------------------------------------------------------
// Copyright (C) 2010 iZsh <izsh at fail0verflow.com>
//
// This code is licensed to you under the terms of the GNU GPL, version 2 or,
// at your option, any later version. See the LICENSE.txt file for the text of
// the license.
//-----------------------------------------------------------------------------
// utilities
//-----------------------------------------------------------------------------

#include "util.h"
#include <stdlib.h>
#include <stdio.h>

#ifdef _WIN32
#include <sysinfoapi.h>
#endif

#define MAX_BIN_BREAK_LENGTH   (3072+384+1)

#ifndef _WIN32
#include <unistd.h>
#endif

// determine number of logical CPU cores (use for multithreaded functions)

// 0 means "not overridden, auto-detect"; every num_CPUs() caller (bit flip
// property threads, hardnested brute force worker threads, progress
// messages) goes through this one function, so setting the override here
// is enough to change the thread count everywhere.
static int forced_num_CPUs = 0;

extern void set_forced_num_CPUs(int n) {
    forced_num_CPUs = n;
}

extern int num_CPUs(void) {
    if (forced_num_CPUs > 0) {
        return forced_num_CPUs;
    }
#ifdef _WIN32
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    return sysinfo.dwNumberOfProcessors;
#else
    return sysconf(_SC_NPROCESSORS_ONLN);
#endif
}

