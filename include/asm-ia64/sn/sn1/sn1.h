/* $Id$
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 - 1997, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2000 by Colin Ngam
 */

/*
 * sn1.h -- hardware specific defines for sn1 boards
 * The defines used here are used to limit the size of 
 * various datastructures in the PROM. eg. KLCFGINFO, MPCONF etc.
 */

#ifndef _ASM_SN_SN1_SN1_H
#define _ASM_SN_SN1_SN1_H

extern xwidgetnum_t hub_widget_id(nasid_t);
extern nasid_t get_nasid(void);
extern int	get_slice(void);
extern int     is_fine_dirmode(void);
extern hubreg_t get_hub_chiprev(nasid_t nasid);
extern hubreg_t get_region(cnodeid_t);
extern hubreg_t nasid_to_region(nasid_t);
extern int      verify_snchip_rev(void);
extern void 	ni_reset_port(void);

#ifdef SN1_USE_POISON_BITS
extern int hub_bte_poison_ok(void);
#endif /* SN1_USE_POISON_BITS */

#endif /* _ASM_SN_SN1_SN1_H */
