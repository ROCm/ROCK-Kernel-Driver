/*****************************************************************************
 *
 * Module Name: ec.h
 *   $Revision: 19 $
 *
 *****************************************************************************/

/*
 *  Copyright (C) 2000, 2001 Andrew Grover
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

#include <linux/spinlock.h>
#include <asm/semaphore.h>
#include <actypes.h>
#include <acexcep.h>
#include <bm.h>


/*****************************************************************************
 *                            Types & Other Defines
 *****************************************************************************/

#define EC_DEFAULT_TIMEOUT	1000		/* 1 second */
#define EC_GPE_UNKNOWN		0xFFFFFFFF
#define EC_PORT_UNKNOWN		0x00000000
#define EC_BURST_ENABLE_ACKNOWLEDGE 0x90


/*
 * Commands:
 * ---------
 */
typedef u8			EC_COMMAND;

#define EC_COMMAND_UNKNOWN	((EC_COMMAND) 0x00)
#define EC_COMMAND_READ		((EC_COMMAND) 0x80)
#define EC_COMMAND_WRITE	((EC_COMMAND) 0x81)
#define EC_COMMAND_QUERY	((EC_COMMAND) 0x84)


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
typedef u32			EC_STATUS;

#define EC_FLAG_OUTPUT_BUFFER	((EC_STATUS) 0x01)
#define EC_FLAG_INPUT_BUFFER	((EC_STATUS) 0x02)
#define EC_FLAG_BURST_MODE	((EC_STATUS) 0x10)
#define EC_FLAG_SCI		((EC_STATUS) 0x20)


/*
 * EC_EVENT:
 * ---------
 */
typedef u32			EC_EVENT;

#define EC_EVENT_UNKNOWN	((EC_EVENT) 0x00)
#define EC_EVENT_NONE		((EC_EVENT) 0x00)
#define EC_EVENT_OUTPUT_BUFFER_FULL ((EC_EVENT) 0x01)
#define EC_EVENT_INPUT_BUFFER_EMPTY ((EC_EVENT) 0x02)
#define EC_EVENT_SCI		((EC_EVENT) 0x03)


/*
 * Hardware IDs:
 * -------------
 */
#define EC_HID_EC		"PNP0C09"


/*
 * EC_REQUEST:
 * -----------
 */
typedef struct
{
	EC_COMMAND              command;
	u8                      address;
	u8                      data;
} EC_REQUEST;


/*
 * Device Context:
 * ---------------
 */
typedef struct
{
	BM_HANDLE               device_handle;
	acpi_handle             acpi_handle;
	u32                     gpe_bit;
	u32			status_port;
	u32			command_port;
	u32			data_port;
	u32			use_global_lock;
	u8                      query_data;
	acpi_handle             mutex;
} EC_CONTEXT;


/*****************************************************************************
 *                             Function Prototypes
 *****************************************************************************/

/* ec.c */

acpi_status
ec_initialize(void);

acpi_status
ec_terminate(void);

acpi_status
ec_notify (
	u32                     notify_type,
	u32                     device,
	void                    **context);

acpi_status
ec_request(
	BM_REQUEST              *request_info,
	void                    *context);

/* ectransx.c */

acpi_status
ec_transaction (
	EC_CONTEXT              *ec,
	EC_REQUEST              *ec_request);

acpi_status
ec_io_read (
	EC_CONTEXT              *ec,
	u32         		io_port,
	u8                      *data,
	EC_EVENT                wait_event);

acpi_status
ec_io_write (
	EC_CONTEXT              *ec,
	u32         		io_port,
	u8                      data,
	EC_EVENT                wait_event);

/* ecgpe.c */

acpi_status
ec_install_gpe_handler (
	EC_CONTEXT              *ec);

acpi_status
ec_remove_gpe_handler (
	EC_CONTEXT              *ec);

/* ecspace.c */

acpi_status
ec_install_space_handler (
	EC_CONTEXT              *ec);

acpi_status
ec_remove_space_handler (
	EC_CONTEXT              *ec);


#endif  /* __EC_H__ */
