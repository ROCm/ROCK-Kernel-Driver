/*
 *
 *    Copyright 2000-2001 MontaVista Software Inc.
 *      Completed implementation.
 *      Author: MontaVista Software, Inc.  <source@mvista.com>
 *              Frank Rowand <frank_rowand@mvista.com>
 *
 *    Module name: redwood.c
 *
 *    Description:
 *
 *      History:  11/09/2001 - Armin
 *      added board_init to add in additional instuctions needed during platfrom_init
 *
 */

#include <linux/config.h>
#include <linux/init.h>
#include <asm/pgtable.h>
#include <asm/ibm4xx.h>
#include <asm/io.h>
#include <asm/machdep.h>

void __init
board_setup_arch(void)
{
}

void __init
board_io_mapping(void)
{
	int i;

	io_block_mapping(OAKNET_IO_VADDR,
			 OAKNET_IO_PADDR, OAKNET_IO_SIZE, _PAGE_IO);

}

void __init
board_setup_irq(void)
{
}

void __init
board_init(void)
{
}

/* hack; blame me dan. -brad */
#ifdef CONFIG_INPUT_KEYBDEV

void
handle_scancode(unsigned char scancode, int down)
{
	printk("handle_scancode(scancode=0x%x, down=%d)\n", scancode, down);
}

static void
kbd_bh(unsigned long dummy)
{
}

DECLARE_TASKLET_DISABLED(keyboard_tasklet, kbd_bh, 0);
void (*kbd_ledfunc) (unsigned int led);

#endif
