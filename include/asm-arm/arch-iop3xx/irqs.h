/*
 * linux/include/asm-arm/arch-iop3xx/irqs.h
 *
 * Copyright:	(C) 2001 MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 */

#include <linux/config.h>

/*
 * Whic iop3xx implementation is this?
 */
#ifdef CONFIG_ARCH_IOP310

#include "iop310-irqs.h"

#else

#include "iop321-irqs.h"

#endif

