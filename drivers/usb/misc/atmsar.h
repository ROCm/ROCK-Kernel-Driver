#ifndef _ATMSAR_H_
#define _ATMSAR_H_

/******************************************************************************
 *  atmsar.h  --  General SAR library for ATM devices.
 *
 *  Copyright (C) 2000, Johan Verrept
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *
 *  You should have received a copy of the GNU General Public License along with
 *  this program; if not, write to the Free Software Foundation, Inc., 59
 *  Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 ******************************************************************************/

#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/atmdev.h>
#include <linux/skbuff.h>
#include <linux/types.h>
#include <linux/atm.h>

#define ATMSAR_USE_53BYTE_CELL  0x1L
#define ATMSAR_SET_PTI          0x2L

#define ATM_CELL_HEADER		(ATM_CELL_SIZE - ATM_CELL_PAYLOAD)

/* types */
#define ATMSAR_TYPE_AAL0        ATM_AAL0
#define ATMSAR_TYPE_AAL1        ATM_AAL1
#define ATMSAR_TYPE_AAL2        ATM_AAL2
#define ATMSAR_TYPE_AAL34       ATM_AAL34
#define ATMSAR_TYPE_AAL5        ATM_AAL5


/* default MTU's */
#define ATMSAR_DEF_MTU_AAL0         48
#define ATMSAR_DEF_MTU_AAL1         47
#define ATMSAR_DEF_MTU_AAL2          0	/* not supported */
#define ATMSAR_DEF_MTU_AAL34         0	/* not supported */
#define ATMSAR_DEF_MTU_AAL5      65535	/* max mtu ..    */

struct atmsar_vcc_data {
	struct atmsar_vcc_data *next;

	/* general atmsar flags, per connection */
	int flags;
	int type;

	/* connection specific non-atmsar data */
	struct atm_vcc *vcc;
	struct k_atm_aal_stats *stats;
	unsigned short mtu;	/* max is actually  65k for AAL5... */

	/* cell data */
	unsigned int vp;
	unsigned int vc;
	unsigned char gfc;
	unsigned char pti;
	unsigned int headerFlags;
	unsigned long atmHeader;

	/* raw cell reassembly */
	struct sk_buff *reasBuffer;
};


extern struct atmsar_vcc_data *atmsar_open (struct atmsar_vcc_data **list, struct atm_vcc *vcc,
					    uint type, ushort vpi, ushort vci, unchar pti,
					    unchar gfc, uint flags);
extern void atmsar_close (struct atmsar_vcc_data **list, struct atmsar_vcc_data *vcc);

struct sk_buff *atmsar_decode_rawcell (struct atmsar_vcc_data *list, struct sk_buff *skb,
				       struct atmsar_vcc_data **ctx);
struct sk_buff *atmsar_decode_aal5 (struct atmsar_vcc_data *ctx, struct sk_buff *skb);

#endif				/* _ATMSAR_H_ */
