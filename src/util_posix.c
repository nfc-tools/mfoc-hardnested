//-----------------------------------------------------------------------------
// Copyright (C) 2010 iZsh <izsh at fail0verflow.com>
//
// This code is licensed to you under the terms of the GNU GPL, version 2 or,
// at your option, any later version. See the LICENSE.txt file for the text of
// the license.
//-----------------------------------------------------------------------------
// utilities requiring Posix library functions
//-----------------------------------------------------------------------------

#include "util_posix.h"
#include <stdint.h>
#include <time.h>
#include <windows.h>
#include <sys/types.h>
#include <sys/timeb.h>
#include <sys/types.h>
#include <sys/timeb.h>

// a milliseconds timer for performance measurement

uint64_t msclock() {
    struct _timeb t;
    _ftime(&t);
    return 1000 * (uint64_t)t.time + t.millitm;
}

