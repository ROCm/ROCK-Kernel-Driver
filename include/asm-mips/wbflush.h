/*
 * Header file for using the wbflush routine
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1998 Harald Koerfgen
 *
 * $Id: wbflush.h,v 1.2 1999/08/13 17:07:28 harald Exp $
 */
#ifndef __ASM_MIPS_WBFLUSH_H
#define __ASM_MIPS_WBFLUSH_H

#include <linux/config.h>

#if defined(CONFIG_CPU_HAS_WB)
/*
 * R2000 or R3000
 */
extern void (*__wbflush) (void);

#define wbflush() __wbflush()

#else
/*
 * we don't need no stinkin' wbflush
 */

#define wbflush()

#endif

extern void wbflush_setup(void);

#endif /* __ASM_MIPS_WBFLUSH_H */
