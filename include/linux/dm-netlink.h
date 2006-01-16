/*
 * Device Mapper Netlink Support
 *
 * Copyright (C) 2005 IBM Corporation
 * 	Author: Mike Anderson <andmike@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */
#ifndef LINUX_DM_NETLINK_H
#define LINUX_DM_NETLINK_H

enum dm_evt_attr {
	DM_E_ATTR_SEQNUM	= 1,
	DM_E_ATTR_TSSEC		= 2,
	DM_E_ATTR_TSUSEC	= 3,
	DM_E_ATTR_DMNAME	= 4,
	DM_E_ATTR_BLKERR	= 5,
	DM_E_ATTR_MAX,
};

#define DM_EVT NLMSG_MIN_TYPE + 0x1

#define DM_EVT_FAIL_PATH 0x1
#define DM_EVT_REINSTATE_PATH 0x2

struct dm_nl_msghdr {
	uint16_t type;
	uint16_t version;
	uint16_t reserved1;
	uint16_t reserved2;
} __attribute__((aligned(sizeof(uint64_t))));

#endif /* LINUX_DM_NETLINK_H */
