/* 
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000-2003 Silicon Graphics, Inc. All rights reserved.
 */


/*
 * Architecture-specific kernel symbols
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <asm/machvec.h>
#include <asm/sn/intr.h>
#include <asm/sn/sgi.h>
#include <asm/sn/types.h>
#include <asm/sn/arch.h>
#include <asm/sn/bte.h>
#include <asm/sal.h>
#include <asm/sn/sn_sal.h>

#ifdef CONFIG_IA64_SGI_SN_DEBUG
EXPORT_SYMBOL(__pa_debug);
EXPORT_SYMBOL(__va_debug);
#endif

EXPORT_SYMBOL(bte_copy);
EXPORT_SYMBOL(bte_unaligned_copy);
EXPORT_SYMBOL(ia64_sal);
EXPORT_SYMBOL(sal_lock);
EXPORT_SYMBOL(sn_local_partid);


