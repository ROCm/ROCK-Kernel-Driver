/*
 *  linux/drivers/net/netconsole.h
 *
 *  Copyright (C) 2001  Ingo Molnar <mingo@redhat.com>
 *
 *  This file contains the implementation of an IRQ-safe, crash-safe
 *  kernel console implementation that outputs kernel messages to the
 *  network.
 *
 * Modification history:
 *
 * 2001-09-17    started by Ingo Molnar.
 */

/****************************************************************
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2, or (at your option)
 *      any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 ****************************************************************/

#define NETCONSOLE_VERSION 0x03

enum netdump_commands {
	COMM_NONE = 0,
	COMM_SEND_MEM = 1,
	COMM_EXIT = 2,
	COMM_REBOOT = 3,
	COMM_HELLO = 4,
	COMM_GET_NR_PAGES = 5,
	COMM_GET_PAGE_SIZE = 6,
	COMM_START_NETDUMP_ACK = 7,
	COMM_GET_REGS = 8,
	COMM_GET_MAGIC = 9,
	COMM_START_WRITE_NETDUMP_ACK = 10,
};

typedef struct netdump_req_s {
	u64 magic;
	u32 nr;
	u32 command;
	u32 from;
	u32 to;
} req_t;

enum netdump_replies {
	REPLY_NONE = 0,
	REPLY_ERROR = 1,
	REPLY_LOG = 2,
	REPLY_MEM = 3,
	REPLY_RESERVED = 4,
	REPLY_HELLO = 5,
	REPLY_NR_PAGES = 6,
	REPLY_PAGE_SIZE = 7,
	REPLY_START_NETDUMP = 8,
	REPLY_END_NETDUMP = 9,
	REPLY_REGS = 10,
	REPLY_MAGIC = 11,
	REPLY_START_WRITE_NETDUMP = 12,
};

typedef struct netdump_reply_s {
	u32 nr;
	u32 code;
	u32 info;
} reply_t;

#define HEADER_LEN (1 + sizeof(reply_t))


