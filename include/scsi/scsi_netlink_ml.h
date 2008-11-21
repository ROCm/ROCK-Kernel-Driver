/*
 *  SCSI Midlayer Netlink Interface
 *
 *  Copyright (C) 2008 Hannes Reinecke, SuSE Linux Products GmbH
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
 *
 */
#ifndef SCSI_NETLINK_ML_H
#define SCSI_NETLINK_ML_H

#include <scsi/scsi_netlink.h>

/*
 * This file intended to be included by both kernel and user space
 */

/*
 * FC Transport Message Types
 */
	/* kernel -> user */
#define ML_NL_SCSI_SENSE			0x0100
	/* user -> kernel */
/* none */


/*
 * Message Structures :
 */

/* macro to round up message lengths to 8byte boundary */
#define SCSI_NL_MSGALIGN(len)		(((len) + 7) & ~7)


/*
 * SCSI Midlayer SCSI Sense messages :
 *   SCSI_NL_SCSI_SENSE
 *
 */
struct scsi_nl_sense_msg {
	struct scsi_nl_hdr snlh;		/* must be 1st element ! */
	uint64_t seconds;
	u64 id;
	u64 lun;
	u16 host_no;
	u16 channel;
	u32 sense;
} __attribute__((aligned(sizeof(uint64_t))));


#endif /* SCSI_NETLINK_ML_H */

