/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2004 Silicon Graphics, Inc. All rights reserved.
 */


/*
 * Cross Partition (XP)'s dbgtk related definitions.
 */


#ifndef _ASM_IA64_SN_XP_DBGTK_H
#define _ASM_IA64_SN_XP_DBGTK_H


#if defined(CONFIG_DBGTK) || defined(CONFIG_DBGTK_MODULE)

#include <linux/dbgtk.h>

#else /* defined(CONFIG_DBGTK) || defined(CONFIG_DBGTK_MODULE) */

#define DECLARE_DPRINTK(_mn, _hl, _dcs, _dps, _sd)
#define EXTERN_DPRINTK(_mn)
#define REG_DPRINTK(_mn)
#define UNREG_DPRINTK(_mn)
#define DPRINTK(_mn, mask, fmt...)
#define DPRINTK_ALWAYS(_mn, mask, fmt...) printk(fmt)

#define DECLARE_DTRACE(_mn, _dtm, _sd)
#define EXTERN_DTRACE(_mn)
#define REG_DTRACE(_mn)
#define UNREG_DTRACE(_mn)
#define DTRACE(_mn, mask, cs, p1, p2)
#define DTRACEI(_mn, mask, cs, p1, p2)
#define DTRACE_L(_mn, mask, cs, p1, p2, p3, p4, p5, p6)
#define DTRACEI_L(_mn, mask, cs, p1, p2, p3, p4, p5, p6)

#define DECLARE_DCOUNTER(_pd, _cn)
#define EXTERN_DCOUNTER(_cn)
#define REG_DCOUNTER(_cn)
#define UNREG_DCOUNTER(_cn)
#define DCOUNT(_cn)
#define DCOUNT_CLEAR(_cn)

#endif /* defined(CONFIG_DBGTK) || defined(CONFIG_DBGTK_MODULE) */


#endif /* _ASM_IA64_SN_XP_DBGTK_H */

