/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */

#ifndef _ASM_SN_SN1_SLOTNUM_H
#define _ASM_SN_SN1_SLOTNUM_H

#define SLOTNUM_MAXLENGTH	16

/*
 * This file attempts to define a slot number space across all slots
 * a IP27 module.  Here, we deal with the top level slots.
 *
 *	Node slots
 *	Router slots
 *	Crosstalk slots
 *
 *	Other slots are children of their parent crosstalk slot:
 *		PCI slots
 *		VME slots
 */
// #include <slotnum.h>

// #ifdef NOTDEF	/* moved to sys/slotnum.h */
#define SLOTNUM_NODE_CLASS	0x00	/* Node   */
#define SLOTNUM_ROUTER_CLASS	0x10	/* Router */
#define SLOTNUM_XTALK_CLASS	0x20	/* Xtalk  */
#define SLOTNUM_MIDPLANE_CLASS	0x30	/* Midplane */
#define SLOTNUM_XBOW_CLASS	0x40	/* Xbow  */
#define SLOTNUM_KNODE_CLASS	0x50	/* Kego node */
#define SLOTNUM_INVALID_CLASS	0xf0	/* Invalid */

#define SLOTNUM_CLASS_MASK	0xf0
#define SLOTNUM_SLOT_MASK	0x0f

#define SLOTNUM_GETCLASS(_sn)	((_sn) & SLOTNUM_CLASS_MASK)
#define SLOTNUM_GETSLOT(_sn)	((_sn) & SLOTNUM_SLOT_MASK)
// #endif	/* NOTDEF */

/* This determines module to pnode mapping. */
/* NODESLOTS_PER_MODULE has changed from 4 to 6
 * to support the 12P 4IO configuration. This change
 * helps in minimum  number of changes to code which
 * depend on the number of node boards within a module.
 */
#define NODESLOTS_PER_MODULE		6
#define NODESLOTS_PER_MODULE_SHFT	2

#define HIGHEST_I2C_VISIBLE_NODESLOT	4
#define	RTRSLOTS_PER_MODULE		2

#if __KERNEL__
#include <asm/sn/xtalk/xtalk.h>

extern slotid_t xbwidget_to_xtslot(int crossbow, int widget);
extern slotid_t hub_slotbits_to_slot(slotid_t slotbits);
extern slotid_t hub_slot_to_crossbow(slotid_t hub_slot);
extern slotid_t router_slotbits_to_slot(slotid_t slotbits);
extern slotid_t get_node_slotid(nasid_t nasid);
extern slotid_t get_my_slotid(void);
extern slotid_t get_node_crossbow(nasid_t);
extern xwidgetnum_t hub_slot_to_widget(slotid_t);
extern void get_slotname(slotid_t, char *);
extern void get_my_slotname(char *);
extern slotid_t get_widget_slotnum(int xbow, int widget);
extern void get_widget_slotname(int, int, char *);
extern void router_slotbits_to_slotname(int, char *);
extern slotid_t meta_router_slotbits_to_slot(slotid_t) ;
extern slotid_t hub_slot_get(void);

extern int node_can_talk_to_elsc(void);

extern int  slot_to_widget(int) ;
#define MAX_IO_SLOT_NUM		12
#define MAX_NODE_SLOT_NUM	4
#define MAX_ROUTER_SLOTNUM	2

#endif /* __KERNEL__ */

#endif /* _ASM_SN_SN1_SLOTNUM_H */
