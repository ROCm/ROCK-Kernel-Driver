/*
 * arch/v850/kernel/rte_nb85e_cb.c -- Midas labs RTE-V850E/NB85E-CB board
 *
 *  Copyright (C) 2001,02  NEC Corporation
 *  Copyright (C) 2001,02  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/bootmem.h>
#include <linux/irq.h>

#include <asm/atomic.h>
#include <asm/page.h>
#include <asm/nb85e.h>
#include <asm/rte_nb85e_cb.h>

#include "mach.h"

void __init mach_get_physical_ram (unsigned long *ram_start,
				   unsigned long *ram_len)
{
	/* We just use SDRAM here; the kernel itself is in SRAM.  */
	*ram_start = SDRAM_ADDR;
	*ram_len = SDRAM_SIZE;
}

void __init mach_reserve_bootmem ()
{
	extern char _root_fs_image_start, _root_fs_image_end;
	u32 root_fs_image_start = (u32)&_root_fs_image_start;
	u32 root_fs_image_end = (u32)&_root_fs_image_end;

	/* Reserve the memory used by the root filesystem image if it's
	   in RAM.  */
	if (root_fs_image_start >= RAM_START && root_fs_image_start < RAM_END)
		reserve_bootmem (root_fs_image_start,
				 root_fs_image_end - root_fs_image_start);
}

void mach_gettimeofday (struct timespec *tv)
{
	tv->tv_sec = 0;
	tv->tv_nsec = 0;
}
