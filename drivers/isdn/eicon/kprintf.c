
/*
 *
 * Copyright (C) Eicon Technology Corporation, 2000.
 *
 * Eicon File Revision :    1.3  
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY OF ANY KIND WHATSOEVER INCLUDING ANY 
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */


/*
 * Source file for kernel interface to kernel log facility
 */


#include "eicon.h"
#include "sys.h"
#include <stdarg.h>
#undef MAX
#undef MIN

#include "divas.h"
#include "divalog.h"
#include "uxio.h"

void    DivasPrintf(char  *fmt, ...)

{
    klog_t      log;            /* log entry buffer */

    va_list     argptr;         /* pointer to additional args */

    va_start(argptr, fmt);

    /* clear log entry */

    memset((void *) &log, 0, sizeof(klog_t));

    log.card = -1;
    log.type = KLOG_TEXT_MSG;

    /* time stamp the entry */

    log.time_stamp = UxTimeGet();

    /* call vsprintf to format the user's information */

    vsnprintf(log.buffer, DIM(log.buffer), fmt, argptr);

    va_end(argptr);

    /* send to the log streams driver and return */

    DivasLogAdd(&log, sizeof(klog_t));

    return;
}
