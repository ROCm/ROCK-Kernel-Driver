/*
 * arch/ppc/platforms/4xx/redwood.c
 *
 * Author: Frank Rowand <frank_rowand@mvista.com>, or source@mvista.com
 *
 * 2000-2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/config.h>
#include <linux/init.h>
#include <asm/pgtable.h>
#include <asm/ibm4xx.h>
#include <asm/io.h>
#include <asm/machdep.h>

void __init
redwood_setup_arch(void)
{
	ppc4xx_setup_arch();

#ifdef CONFIG_IDE
	void *xilinx, *xilinx_1, *xilinx_2;
	unsigned short reg5;

	xilinx = ioremap(IDE_XLINUX_MUX_BASE, 0x10);

	/* init xilinx control registers - enable ide mux, clear reset bit */
	if (!xilinx) {
		printk(KERN_CRIT
		       "redwood4_setup_arch() xilinx ioremap failed\n");
		return;
	}
	xilinx_1 = xilinx;
	xilinx_2 = xilinx + 0xe;

	reg5 = readw(xilinx_1);
	writeb(reg5 |= ~(0x8001), xilinx_1);
	writeb(0, xilinx_2);

	udelay(10 * 1000);

	writeb(reg5 & 0x8001, xilinx_1);
	writeb(0, xilinx_2);
	
       	/* add RE & OEN to value set by boot rom */
        mtdcr(DCRN_BRCR3, 0x407cfffe);

#endif

}

void __init
redwood_map_io(void)
{
	int i;

	ppc4xx_map_io();
	io_block_mapping(OAKNET_IO_VADDR,
			 OAKNET_IO_PADDR, OAKNET_IO_SIZE, _PAGE_IO);

}

void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{
	ppc4xx_init(r3, r4, r5, r6, r7);

	ppc_md.setup_arch = redwood_setup_arch;
	ppc_md.setup_io_mappings = redwood_map_io;
}
