/* 
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000-2003 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <asm/io.h>
#include <asm/delay.h>
#include <asm/sn/nodepda.h>
#include <asm/sn/simulator.h>
#include <asm/sn/pda.h>
#include <asm/sn/sn_cpuid.h>
#include <asm/sn/shub_mmr.h>

/**
 * sn_io_addr - convert an in/out port to an i/o address
 * @port: port to convert
 *
 * Legacy in/out instructions are converted to ld/st instructions
 * on IA64.  This routine will convert a port number into a valid 
 * SN i/o address.  Used by sn_in*() and sn_out*().
 */
void *sn_io_addr(unsigned long port)
{
	if (!IS_RUNNING_ON_SIMULATOR()) {
		/* On sn2, legacy I/O ports don't point at anything */
		if (port < (64 * 1024))
			return NULL;
		return ((void *)(port | __IA64_UNCACHED_OFFSET));
	} else {
		/* but the simulator uses them... */
		unsigned long io_base;
		unsigned long addr;

		/*
		 * word align port, but need more than 10 bits
		 * for accessing registers in bedrock local block
		 * (so we don't do port&0xfff)
		 */
		if ((port >= 0x1f0 && port <= 0x1f7) ||
		    port == 0x3f6 || port == 0x3f7) {
			io_base = (0xc000000fcc000000UL |
				   ((unsigned long)get_nasid() << 38));
			addr = io_base | ((port >> 2) << 12) | (port & 0xfff);
		} else {
			addr = __ia64_get_io_port_base() | ((port >> 2) << 2);
		}
		return (void *)addr;
	}
}

EXPORT_SYMBOL(sn_io_addr);

/**
 * __sn_mmiowb - I/O space memory barrier
 *
 * See include/asm-ia64/io.h and Documentation/DocBook/deviceiobook.tmpl
 * for details.
 *
 * On SN2, we wait for the PIO_WRITE_STATUS SHub register to clear.
 * See PV 871084 for details about the WAR about zero value.
 *
 */
void __sn_mmiowb(void)
{
	while ((((volatile unsigned long)(*pda->pio_write_status_addr)) &
		SH_PIO_WRITE_STATUS_0_PENDING_WRITE_COUNT_MASK) !=
	       SH_PIO_WRITE_STATUS_0_PENDING_WRITE_COUNT_MASK)
		cpu_relax();
}

EXPORT_SYMBOL(__sn_mmiowb);
