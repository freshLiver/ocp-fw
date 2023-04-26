//////////////////////////////////////////////////////////////////////////////////
// debug.h for Cosmos+ OpenSSD
// Copyright (c) 2016 Hanyang University ENC Lab.
// Contributed by Yong Ho Song <yhsong@enc.hanyang.ac.kr>
//				  Youngjin Jo <yjjo@enc.hanyang.ac.kr>
//				  Sangjin Lee <sjlee@enc.hanyang.ac.kr>
//
// This file is part of Cosmos+ OpenSSD.
//
// Cosmos+ OpenSSD is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3, or (at your option)
// any later version.
//
// Cosmos+ OpenSSD is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Cosmos+ OpenSSD; see the file COPYING.
// If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
// Company: ENC Lab. <http://enc.hanyang.ac.kr>
// Engineer: Sangjin Lee <sjlee@enc.hanyang.ac.kr>
//
// Project Name: Cosmos+ OpenSSD
// Design Name: Cosmos+ Firmware
// Module Name: Debug Mate
// File Name: debug.h
//
// Version: v1.0.0
//
// Description:
//   - defines macros for easy debugging
//////////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////
// Revision History:
//
// * v1.0.0
//   - First draft
//////////////////////////////////////////////////////////////////////////////////

#ifndef __DEBUG_H_
#define __DEBUG_H_

#include "stdint.h"
#include "xil_printf.h"

/* -------------------------------------------------------------------------- */
/*                              ANSI escape code                              */
/* -------------------------------------------------------------------------- */

#define PR_RESET "\e[0m"
#define PR_DEBUG "\e[32;49;1mDEBUG "
#define PR_INFO  "\e[34;49;1mINFO "
#define PR_WARN  "\e[33;49;1mWARN "
#define PR_ERROR "\e[31;49;1mERROR "

/* -------------------------------------------------------------------------- */
/*                               logging macros                               */
/* -------------------------------------------------------------------------- */

#define CODE_POS_FMT  "(%s() at %s:%d):: "
#define CODE_POS_ARGS __func__, __FILE__, __LINE__
#define SPLIT_LINE    "-----------------------------------------------------------------------------\n"

#ifdef HOST_DEBUG
#include <stdio.h>
#define xil_printf printf
#else
#define printf xil_printf
#endif

#define pr_raw       xil_printf
#define pr(fmt, ...) pr_raw(fmt "\r\n", ##__VA_ARGS__)

#ifdef DEBUG
#define pr_debug(fmt, ...) pr(PR_DEBUG CODE_POS_FMT PR_RESET fmt, CODE_POS_ARGS, ##__VA_ARGS__)
#else
#define pr_debug(fmt, ...)
#endif
#define pr_info(fmt, ...)  pr(PR_INFO PR_RESET fmt, ##__VA_ARGS__)
#define pr_warn(fmt, ...)  pr(PR_WARN CODE_POS_FMT PR_RESET fmt, CODE_POS_ARGS, ##__VA_ARGS__)
#define pr_error(fmt, ...) pr(PR_ERROR CODE_POS_FMT PR_RESET fmt, CODE_POS_ARGS, ##__VA_ARGS__)

/* -------------------------------------------------------------------------- */
/*                              debugging macros                              */
/* -------------------------------------------------------------------------- */

#define MEMBER_SIZE(type, mem) (sizeof((((type *)0)->mem)))

#define ASSERT(cond, ...)                                                                                         \
    ({                                                                                                            \
        if (!(cond))                                                                                              \
        {                                                                                                         \
            pr_error("assert failed: " __VA_ARGS__);                                                              \
            while (1)                                                                                             \
                ;                                                                                                 \
        }                                                                                                         \
    })

// raise compiler error when (cond == 0), must place in a function
#define STATIC_ASSERT(cond) ((int)(sizeof(struct { int : (-!(cond)); })))

#endif
