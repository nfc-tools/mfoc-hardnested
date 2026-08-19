//-----------------------------------------------------------------------------
// Copyright (C) 2010 iZsh <izsh at fail0verflow.com>
//
// This code is licensed to you under the terms of the GNU GPL, version 2 or,
// at your option, any later version. See the LICENSE.txt file for the text of
// the license.
//-----------------------------------------------------------------------------
// utilities
//-----------------------------------------------------------------------------

#ifndef UTIL_H__
#define UTIL_H__

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

extern int num_CPUs(void); // number of logical CPUs, or the forced override if set
extern void set_forced_num_CPUs(int n); // override num_CPUs(); pass 0 to go back to auto-detect

#endif // UTIL_H__
