/*
 *  Copyright (C) 2000 Andrew Grover
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */


#ifndef __EC_H__
#define __EC_H__

// TODO: Linux-specific
#include <linux/spinlock.h>
#include <asm/semaphore.h>

#include <actypes.h>
#include <acexcep.h>

/*****************************************************************************
 *                             Types & Other Defines
 *****************************************************************************/

#define EC_DEFAULT_TIMEOUT              1000            /* 1 second */
#define EC_GPE_UNKNOWN                  0xFFFFFFFF
#define EC_PORT_UNKNOWN                 0x00000000
#define EC_BURST_ENABLE_ACKNOWLEDGE     0x90

/* 
 * EC_COMMAND:
 * -----------
 */
typedef UINT8                           EC_COMMAND;

#define EC_COMMAND_UNKNOWN              ((EC_COMMAND) 0x00)
#define EC_COMMAND_READ                 ((EC_COMMAND) 0x80)
#define EC_COMMAND_WRITE                ((EC_COMMAND) 0x81)
#define EC_COMMAND_QUERY                ((EC_COMMAND) 0x84)

/*
 * EC_STATUS:
 * ----------
 * The encoding of the EC status register is illustrated below.
 * Note that a set bit (1) indicates the property is TRUE
 * (e.g. if bit 0 is set then the output buffer is full).
 * +-+-+-+-+-+-+-+-+
 * |7|6|5|4|3|2|1|0|    
 * +-+-+-+-+-+-+-+-+
 *  | | | | | | | |
 *  | | | | | | | +- Output Buffer Full (OBF)?
 *  | | | | | | +--- Input Buffer Full (IBF)?
 *  | | | | | +----- <reserved>
 *  | | | | +------- data Register is command Byte?
 *  | | | +--------- Burst Mode Enabled?
 *  | | +----------- SCI event?
 *  | +------------- SMI event?
 *  +--------------- <Reserved>
 *
 */
typedef UINT8                           EC_STATUS;

#define EC_FLAG_OUTPUT_BUFFER           ((EC_STATUS) 0x01)
#define EC_FLAG_INPUT_BUFFER            ((EC_STATUS) 0x02)
#define EC_FLAG_BURST_MODE              ((EC_STATUS) 0x10)
#define EC_FLAG_SCI                     ((EC_STATUS) 0x20)

/* 
 * EC_EVENT:
 * ---------
 */
typedef UINT8                           EC_EVENT;

#define EC_EVENT_UNKNOWN                ((EC_EVENT) 0x00)
#define EC_EVENT_NONE                   ((EC_EVENT) 0x00)
#define EC_EVENT_OUTPUT_BUFFER_FULL		((EC_EVENT) 0x01)
#define EC_EVENT_INPUT_BUFFER_EMPTY  	((EC_EVENT) 0x02)
#define EC_EVENT_SCI                    ((EC_EVENT) 0x03)

/*
 * EC_REQUEST:
 * -----------
 */
typedef struct
{
    EC_COMMAND              command;
    UINT8                   address;
    UINT8                   data;
} EC_REQUEST;

#endif  /* __EC_H__ */
