/******************************************************************************
 *  atmsar.c  --  General SAR library for ATM devices.
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

/*
 *  Written by Johan Verrept (Johan.Verrept@advalvas.be)
 *
 *  0.2.4A:	- Version for inclusion in 2.5 series kernel
 *		- Modifications by Richard Purdie (rpurdie@rpsys.net)
 *		- replaced "sarlib" with "atmsar"
 *		- adaptations for inclusion in kernel tree
 *
 *  0.2.4:	- Fixed wrong buffer overrun check in atmsar_decode_rawcell()
 *		reported by Stephen Robinson <stephen.robinson@zen.co.uk>
 *		- Fixed bug when input skb did not contain a multple of 52/53
 *		bytes (would happen when the speedtouch device resynced)
 *		also reported by Stephen Robinson <stephen.robinson@zen.co.uk>
 *
 *  0.2.3:	- Fixed wrong allocation size. caused memory corruption in some
 *		cases. Reported by Vladimir Dergachev <volodya@mindspring.com>
 *		- Added some comments
 *
 *  0.2.2:	- Fixed CRCASM
 *		patch from Linus Flannagan <linusf@netservices.eng.net>
 *		- Fixed problem when user did NOT use the
 *		ATMSAR_USE_53BYTE_CELL flag.
 *		reported by  Piers Scannell <email@lot105.com>
 *		- No more in-buffer rewriting for cloned buffers.
 *		- Removed the PII specific CFLAGS in the Makefile.
 *
 *  0.2.1:	- removed dependency on alloc_tx. tis presented problems when
 *		using this with the br2684 code.
 *
 *  0.2:	- added AAL0 reassembly
 *		- added alloc_tx support
 *		- replaced alloc_skb in decode functions to dev_alloc_skb to
 *		allow calling from interrupt
 *		- fixed embarassing AAL5 bug. I was setting the pti bit in the
 *		wrong byte...
 *		- fixed another emabrassing bug.. picked up the wrong crc type
 *		and forgot to invert the crc result...
 *		- fixed AAL5 length calculations.
 *		- removed automatic skb freeing from encode functions.
 *		This caused problems because i did kfree_skb it, while it
 *		needed to be popped. I cannot determine though whether it
 *		needs to be popped or not. Figu'e it out ye'self ;-)
 *		- added mru field. This is the buffersize. atmsar_decode_aal0
 *		will use when it allocates a receive buffer. A stop gap for
 *		real buffer management.
 *
 *  0.1:	- library created.
 *		- only contains AAL5, AAL0 can be easily added. (actually, only
 *		AAL0 reassembly is missing)
 *
 */

#include <linux/crc32.h>
#include "atmsar.h"

/***********************
 **
 **  things to remember
 **
 ***********************/

/*
  1. the atmsar_vcc_data list pointer MUST be initialized to NULL
  2. atmsar_encode_rawcell will drop incomplete cells.
  3. ownership of the skb goes to the library !
*/

#define ATM_HDR_VPVC_MASK  (ATM_HDR_VPI_MASK | ATM_HDR_VCI_MASK)

/***********************
 **
 **  LOCAL STRUCTURES
 **
 ***********************/

/***********************
 **
 **  LOCAL MACROS
 **
 ***********************/
/*
#define DEBUG 1
*/
#ifdef DEBUG
#define PDEBUG(arg...)  printk(KERN_DEBUG "atmsar: " arg)
#else
#define PDEBUG(arg...)
#endif

#define ADD_HEADER(dest, header) \
  *dest++ = (unsigned char) (header >> 24); \
  *dest++ = (unsigned char) (header >> 16); \
  *dest++ = (unsigned char) (header >> 8); \
  *dest++ = (unsigned char) (header & 0xff);


struct atmsar_vcc_data *atmsar_open (struct atmsar_vcc_data **list, struct atm_vcc *vcc, uint type,
				     ushort vpi, ushort vci, unchar pti, unchar gfc, uint flags)
{
	struct atmsar_vcc_data *new;

	if (!vcc)
		return NULL;

	new = kmalloc (sizeof (struct atmsar_vcc_data), GFP_KERNEL);

	if (!new)
		return NULL;

	memset (new, 0, sizeof (struct atmsar_vcc_data));
	new->vcc = vcc;
	new->stats = vcc->stats;
	new->type = type;
	new->next = NULL;
	new->gfc = gfc;
	new->vp = vpi;
	new->vc = vci;
	new->pti = pti;

	switch (type) {
	case ATMSAR_TYPE_AAL0:
		new->mtu = ATMSAR_DEF_MTU_AAL0;
		break;
	case ATMSAR_TYPE_AAL1:
		new->mtu = ATMSAR_DEF_MTU_AAL1;
		break;
	case ATMSAR_TYPE_AAL2:
		new->mtu = ATMSAR_DEF_MTU_AAL2;
		break;
	case ATMSAR_TYPE_AAL34:
		/* not supported */
		new->mtu = ATMSAR_DEF_MTU_AAL34;
		break;
	case ATMSAR_TYPE_AAL5:
		new->mtu = ATMSAR_DEF_MTU_AAL5;
		break;
	}

	new->atmHeader = ((unsigned long) gfc << ATM_HDR_GFC_SHIFT)
	    | ((unsigned long) vpi << ATM_HDR_VPI_SHIFT)
	    | ((unsigned long) vci << ATM_HDR_VCI_SHIFT)
	    | ((unsigned long) pti << ATM_HDR_PTI_SHIFT);
	new->flags = flags;
	new->next = NULL;
	new->reasBuffer = NULL;

	new->next = *list;
	*list = new;

	PDEBUG ("Allocated new SARLib vcc 0x%p with vp %d vc %d\n", new, vpi, vci);

	return new;
}

void atmsar_close (struct atmsar_vcc_data **list, struct atmsar_vcc_data *vcc)
{
	struct atmsar_vcc_data *work;

	if (*list == vcc) {
		*list = (*list)->next;
	} else {
		for (work = *list; work && work->next && (work->next != vcc); work = work->next);

		/* return if not found */
		if (work->next != vcc)
			return;

		work->next = work->next->next;
	}

	if (vcc->reasBuffer) {
		dev_kfree_skb (vcc->reasBuffer);
	}

	PDEBUG ("Allocated SARLib vcc 0x%p with vp %d vc %d\n", vcc, vcc->vp, vcc->vc);

	kfree (vcc);
}


/***********************
 **
 **  DECODE FUNCTIONS
 **
 ***********************/

struct sk_buff *atmsar_decode_rawcell (struct atmsar_vcc_data *list, struct sk_buff *skb,
				       struct atmsar_vcc_data **ctx)
{
	while (skb->len) {
		unsigned char *cell = skb->data;
		unsigned char *cell_payload;
		struct atmsar_vcc_data *vcc = list;
		unsigned long atmHeader =
		    ((unsigned long) (cell[0]) << 24) | ((unsigned long) (cell[1]) << 16) |
		    ((unsigned long) (cell[2]) << 8) | (cell[3] & 0xff);

		PDEBUG ("atmsar_decode_rawcell (0x%p, 0x%p, 0x%p) called\n", list, skb, ctx);
		PDEBUG ("atmsar_decode_rawcell skb->data %p, skb->tail %p\n", skb->data, skb->tail);

		if (!list || !skb || !ctx)
			return NULL;
		if (!skb->data || !skb->tail)
			return NULL;

		/* here should the header CRC check be... */

		/* look up correct vcc */
		for (;
		     vcc
		     && ((vcc->atmHeader & ATM_HDR_VPVC_MASK) != (atmHeader & ATM_HDR_VPVC_MASK));
		     vcc = vcc->next);

		PDEBUG ("atmsar_decode_rawcell found vcc %p for packet on vp %d, vc %d\n", vcc,
			(int) ((atmHeader & ATM_HDR_VPI_MASK) >> ATM_HDR_VPI_SHIFT),
			(int) ((atmHeader & ATM_HDR_VCI_MASK) >> ATM_HDR_VCI_SHIFT));

		if (vcc && (skb->len >= (vcc->flags & ATMSAR_USE_53BYTE_CELL ? 53 : 52))) {
			cell_payload = cell + (vcc->flags & ATMSAR_USE_53BYTE_CELL ? 5 : 4);

			switch (vcc->type) {
			case ATMSAR_TYPE_AAL0:
				/* case ATMSAR_TYPE_AAL1: when we have a decode AAL1 function... */
				{
					struct sk_buff *tmp = dev_alloc_skb (vcc->mtu);

					if (tmp) {
						memcpy (tmp->tail, cell_payload, 48);
						skb_put (tmp, 48);

						if (vcc->stats)
							atomic_inc (&vcc->stats->rx);

						skb_pull (skb,
							  (vcc->
							   flags & ATMSAR_USE_53BYTE_CELL ? 53 :
							   52));
						PDEBUG
						    ("atmsar_decode_rawcell returns ATMSAR_TYPE_AAL0 pdu 0x%p with length %d\n",
						     tmp, tmp->len);
						return tmp;
					};
				}
				break;
			case ATMSAR_TYPE_AAL1:
			case ATMSAR_TYPE_AAL2:
			case ATMSAR_TYPE_AAL34:
				/* not supported */
				break;
			case ATMSAR_TYPE_AAL5:
				if (!vcc->reasBuffer)
					vcc->reasBuffer = dev_alloc_skb (vcc->mtu);

				/* if alloc fails, we just drop the cell. it is possible that we can still
				 * receive cells on other vcc's
				 */
				if (vcc->reasBuffer) {
					/* if (buffer overrun) discard received cells until now */
					if ((vcc->reasBuffer->len) > (vcc->mtu - 48))
						skb_trim (vcc->reasBuffer, 0);

					/* copy data */
					memcpy (vcc->reasBuffer->tail, cell_payload, 48);
					skb_put (vcc->reasBuffer, 48);

					/* check for end of buffer */
					if (cell[3] & 0x2) {
						struct sk_buff *tmp;

						/* the aal5 buffer ends here, cut the buffer. */
						/* buffer will always have at least one whole cell, so */
						/* don't need to check return from skb_pull */
						skb_pull (skb,
							  (vcc->
							   flags & ATMSAR_USE_53BYTE_CELL ? 53 :
							   52));
						*ctx = vcc;
						tmp = vcc->reasBuffer;
						vcc->reasBuffer = NULL;

						PDEBUG
						    ("atmsar_decode_rawcell returns ATMSAR_TYPE_AAL5 pdu 0x%p with length %d\n",
						     tmp, tmp->len);
						return tmp;
					}
				}
				break;
			};
			/* flush the cell */
			/* buffer will always contain at least one whole cell, so don't */
			/* need to check return value from skb_pull */
			skb_pull (skb, (vcc->flags & ATMSAR_USE_53BYTE_CELL ? 53 : 52));
		} else {
			/* If data is corrupt and skb doesn't hold a whole cell, flush the lot */
			if (skb_pull (skb, (list->flags & ATMSAR_USE_53BYTE_CELL ? 53 : 52)) ==
			    NULL)
				return NULL;
		}
	}

	return NULL;
};

struct sk_buff *atmsar_decode_aal5 (struct atmsar_vcc_data *ctx, struct sk_buff *skb)
{
	uint crc = 0xffffffff;
	uint length, pdu_crc, pdu_length;

	PDEBUG ("atmsar_decode_aal5 (0x%p, 0x%p) called\n", ctx, skb);

	if (skb->len && (skb->len % 48))
		return NULL;

	length = (skb->tail[-6] << 8) + skb->tail[-5];
	pdu_crc =
	    (skb->tail[-4] << 24) + (skb->tail[-3] << 16) + (skb->tail[-2] << 8) + skb->tail[-1];
	pdu_length = ((length + 47 + 8) / 48) * 48;

	PDEBUG ("atmsar_decode_aal5: skb->len = %d, length = %d, pdu_crc = 0x%x, pdu_length = %d\n",
		skb->len, length, pdu_crc, pdu_length);

	/* is skb long enough ? */
	if (skb->len < pdu_length) {
		if (ctx->stats)
			atomic_inc (&ctx->stats->rx_err);
		return NULL;
	}

	/* is skb too long ? */
	if (skb->len > pdu_length) {
		PDEBUG ("atmsar_decode_aal5: Warning: readjusting illeagl size %d -> %d\n",
			skb->len, pdu_length);
		/* buffer is too long. we can try to recover
		 * if we discard the first part of the skb.
		 * the crc will decide whether this was ok
		 */
		skb_pull (skb, skb->len - pdu_length);
	}

	crc = ~crc32_be (crc, skb->data, pdu_length - 4);

	/* check crc */
	if (pdu_crc != crc) {
		PDEBUG ("atmsar_decode_aal5: crc check failed!\n");
		if (ctx->stats)
			atomic_inc (&ctx->stats->rx_err);
		return NULL;
	}

	/* pdu is ok */
	skb_trim (skb, length);

	/* update stats */
	if (ctx->stats)
		atomic_inc (&ctx->stats->rx);

	PDEBUG ("atmsar_decode_aal5 returns pdu 0x%p with length %d\n", skb, skb->len);
	return skb;
};
