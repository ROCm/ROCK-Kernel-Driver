
/*
 *
 * Copyright (C) Eicon Technology Corporation, 2000.
 *
 * This source file is supplied for the exclusive use with Eicon
 * Technology Corporation's range of DIVA Server Adapters.
 *
 * Eicon File Revision :    1.0  
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
 * Include file for defining the kernel loggger messages
 * These definitions are shared between the klog driver and the
 * klogd daemon process
 */

#if !defined(_KLOGMSG_H)
#define _KLOGMSG_H

/* define a type for a log entry */

#define KLOG_TEXT_MSG   	(0)
#define KLOG_XLOG_MSG   	(1)
#define KLOG_XTXT_MSG   	(2)
#define KLOG_IDI_REQ   		(4)
#define KLOG_IDI_CALLBACK   (5)
#define KLOG_CAPI_MSG   	(6)

typedef struct
{
    unsigned long   time_stamp; /* in ms since last system boot */
    int    			card;       /* card number (-1 for all) */
    unsigned int    type;       /* type of log message (0 is text) */
    unsigned int    length;     /* message length (non-text messages only) */
    unsigned short  code;       /* message code (non-text messages only) */
    char            buffer[110];/* text/data to log */
} klog_t;

void    DivasLogAdd(void *buffer, int length);
#endif /* of _KLOGMSG_H */
