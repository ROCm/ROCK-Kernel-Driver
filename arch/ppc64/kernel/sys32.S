/* 
 * sys32.S: I-cache tricks for 32-bit compatibility layer simple
 *          conversions.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1998 Jakub Jelinek   (jj@ultra.linux.cz)
 * Copyright (C) 2000 Ken Aaker (kdaaker@rchland.vnet.ibm.com)
 * For PPC ABI convention is parms in Regs 3-10.
 * The router in entry.S clears the high 32 bits in the first
 * 4 arguments (R3-R6).
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <asm/ppc_asm.h>
#include <asm/errno.h>
#include <asm/processor.h>

	.text

_GLOBAL(ppc32_lseek)
	extsw		r4,r4		/* sign extend off_t offset parm */
	b		.sys_lseek

