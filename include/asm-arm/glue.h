/*
 *  linux/include/asm-arm/glue.h
 *
 *  Copyright (C) 1997-1999 Russell King
 *  Copyright (C) 2000-2002 Deep Blue Solutions Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  This file provides the glue to stick the processor-specific bits
 *  into the kernel in an efficient manner.  The idea is to use branches
 *  when we're only targetting one class of TLB, or indirect calls
 *  when we're targetting multiple classes of TLBs.
 */
#ifdef __KERNEL__

#include <linux/config.h>

#ifdef __STDC__
#define ____glue(name,fn)	name##fn
#else
#define ____glue(name,fn)	name/**/fn
#endif
#define __glue(name,fn)		____glue(name,fn)

/*
 * Select MMU TLB handling.
 */

/*
 * ARMv3 MMU
 */
#undef _TLB
#if defined(CONFIG_CPU_ARM610) || defined(CONFIG_CPU_ARM710)
# ifdef _TLB
#  define MULTI_TLB 1
# else
#  define _TLB v3
# endif
#endif

/*
 * ARMv4 MMU without write buffer
 */
#if defined(CONFIG_CPU_ARM720T)
# ifdef _TLB
#  define MULTI_TLB 1
# else
#  define _TLB v4
# endif
#endif

/*
 * ARMv4 MMU with write buffer, with invalidate I TLB entry instruction
 */
#if defined(CONFIG_CPU_ARM920T) || defined(CONFIG_CPU_ARM922T) || \
    defined(CONFIG_CPU_ARM926T) || defined(CONFIG_CPU_ARM1020) || \
    defined(CONFIG_CPU_XSCALE)
# ifdef _TLB
#  define MULTI_TLB 1
# else
#  define _TLB v4wbi
# endif
#endif

/*
 * ARMv4 MMU with write buffer, without invalidate I TLB entry instruction
 */
#if defined(CONFIG_CPU_SA110) || defined(CONFIG_CPU_SA1100)
# ifdef _TLB
#  define MULTI_TLB 1
# else
#  define _TLB v4wb
# endif
#endif

#endif
