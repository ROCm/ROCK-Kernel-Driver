/*
 *  linux/include/asm-arm/numnodes.h
 *
 *  Copyright (C) 2002 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_ARM_NUMNODES_H
#define __ASM_ARM_NUMNODES_H

#ifdef CONFIG_ARCH_LH7A40X
# define NODES_SHIFT	4	/* Max 16 nodes for the Sharp CPUs */
#else
# define NODES_SHIFT	2	/* Normally, Max 4 Nodes */
#endif

#endif
