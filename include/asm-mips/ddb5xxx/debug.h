/***********************************************************************
 *
 * Copyright 2001 MontaVista Software Inc.
 * Author: jsun@mvista.com or jsun@junsun.net
 *
 * include/asm-mips/ddb5xxx/debug.h
 *     Some debug macros used by ddb code.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 ***********************************************************************
 */

#ifndef __ASM_DDB5XXX_DEBUG_H
#define __ASM_DDB5XXX_DEBUG_H

#include <linux/config.h>

/*
 * macro for catching spurious errors.  Eable to LL_DEBUG in kernel hacking
 * config menu.
 */
#ifdef CONFIG_LL_DEBUG

#include <linux/kernel.h>

#define MIPS_ASSERT(x)  if (!(x)) { panic("MIPS_ASSERT failed at %s:%d\n", __FILE__, __LINE__); }
#define MIPS_VERIFY(x, y) MIPS_ASSERT(x y)
#define MIPS_DEBUG(x)  do { x; } while (0)

#else

#define MIPS_ASSERT(x)
#define MIPS_VERIFY(x, y) x
#define MIPS_DEBUG(x)

#endif

#endif /* __ASM_DDB5XXX_DEBUG_H */
