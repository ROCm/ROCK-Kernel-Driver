/*
 * include/asm-v850/setup.h
 *
 *  Copyright (C) 2001  NEC Corporation
 *  Copyright (C) 2001  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#ifndef __V850_SETUP_H__
#define __V850_SETUP_H__

/* Linux/v850 platforms.  This corresponds roughly to what the outside
   the CPU looks like.  */
#define MACH_SIM	1	/* GDB architectural simulator */

/* v850 cpu architectures.  This is what a user-program would be
   concerned with.  */
#define CPU_ARCH_V850E	1
#define CPU_ARCH_V850E2	2

/* v850 cpu `cores'.  These are system-level extensions to the basic CPU,
   defining such things as interrupt-handling.  */
#define CPU_CORE_NB85E	1
#define CPU_CORE_NB85ET	2
#define CPU_CORE_NU85E	3
#define CPU_CORE_NU85ET	4

/* Specific v850 cpu chips.  These each incorporate a `core', and add
   varions peripheral services.  */
#define CPU_CHIP_MA1	1

#endif /* __V850_SETUP_H__ */
