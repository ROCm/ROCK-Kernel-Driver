/* 
   BlueZ - Bluetooth protocol stack for Linux
   Copyright (C) 2000-2001 Qualcomm Incorporated

   Written 2000,2001 by Maxim Krasnyansky <maxk@qualcomm.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 as
   published by the Free Software Foundation;

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS.
   IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) AND AUTHOR(S) BE LIABLE FOR ANY
   CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES 
   WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN 
   ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF 
   OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

   ALL LIABILITY, INCLUDING LIABILITY FOR INFRINGEMENT OF ANY PATENTS, 
   COPYRIGHTS, TRADEMARKS OR OTHER RIGHTS, RELATING TO USE OF THIS 
   SOFTWARE IS DISCLAIMED.
*/

/*
 *  $Id: bluetooth.h,v 1.1 2001/06/01 08:12:11 davem Exp $
 */

#ifndef __IF_BLUETOOTH_H
#define __IF_BLUETOOTH_H

#include <asm/types.h>
#include <asm/byteorder.h>

#define BTPROTO_L2CAP   0
#define BTPROTO_HCI     1

#define SOL_HCI     0
#define SOL_L2CAP   6

typedef struct {
	__u8 b0, b1, b2, b3, b4, b5;
} bdaddr_t;

#define BDADDR_ANY ((bdaddr_t *)"\000\000\000\000\000")

/* Connection and socket states */
enum {
	BT_CONNECTED = 1, /* Equal to TCP_ESTABLISHED to make net code happy */
	BT_OPEN,
	BT_BOUND,
	BT_LISTEN,
	BT_CONNECT,
	BT_CONFIG,
	BT_DISCONN,
	BT_CLOSED
};

/* Copy, swap, convert BD Address */
static __inline__ int bacmp(bdaddr_t *ba1, bdaddr_t *ba2)
{
	return memcmp(ba1, ba2, sizeof(bdaddr_t));
}
static __inline__ void bacpy(bdaddr_t *dst, bdaddr_t *src)
{
	memcpy(dst, src, sizeof(bdaddr_t));
}

extern void baswap(bdaddr_t *dst, bdaddr_t *src);

extern char *batostr(bdaddr_t *ba);
extern bdaddr_t *strtoba(char *str);

/* Endianness conversions */
#define htobs(a)	__cpu_to_le16(a)
#define htobl(a)	__cpu_to_le32(a)
#define btohs(a)	__le16_to_cpu(a)
#define btohl(a)	__le32_to_cpu(a)

int bterr(__u16 code);

#endif /* __IF_BLUETOOTH_H */
