/*
 * $Id: rpckbd.c,v 1.7 2001/09/25 10:12:07 vojtech Exp $
 *
 *  Copyright (c) 2000-2001 Vojtech Pavlik
 *
 *  Based on the work of:
 *	unknown author
 */

/*
 * Acorn RiscPC PS/2 keyboard controller driver for Linux/ARM
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@ucw.cz>, or by paper mail:
 * Vojtech Pavlik, Simunkova 1594, Prague 8, 182 00 Czech Republic
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/serio.h>

#include <asm/irq.h>
#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/iomd.h>
#include <asm/system.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("Acorn RiscPC PS/2 keyboard controller driver");
MODULE_LICENSE("GPL");

static inline void rpckbd_write(unsigned char val)
{
	while(!(inb(IOMD_KCTRL) & (1 << 7)));
	outb(val, IOMD_KARTTX);
}

static struct serio rpckbd_port =
{
	type:	SERIO_8042,
	write:	rpckbd_write,
	name: 	"RiscPC PS/2 kbd port",
	phys:	"rpckbd/serio0",
};

static void rpckbd_rx(int irq, void *dev_id, struct pt_regs *regs)
{
	kbd_pt_regs = regs;

	while (inb(IOMD_KCTRL) & (1 << 5))
		if (rpckbd_port.dev)
               		 rpckbd_port.dev->interrupt(&rpckbd_port, inb(IOMD_KARTRX), 0);

}

static void rpckbd_tx(int irq, void *dev_id, struct pt_regs *regs)
{
}

static int __init rpckbd_init(void)
{
	unsigned long flags;

	/* Reset the keyboard state machine. */
	outb(0, IOMD_KCTRL);
	outb(8, IOMD_KCTRL);

	save_flags_cli(flags);

	if (request_irq(IRQ_KEYBOARDRX, rpckbd_rx, 0, "rpckbd", NULL) != 0) {
		printk(KERN_ERR "rpckbd.c: Could not allocate keyboard receive IRQ!\n")
		return -EBUSY;
	}

	if (request_irq(IRQ_KEYBOARDTX, rpckbd_tx, 0, "rpckbd", NULL) != 0) {
		printk(KERN_ERR "rpckbd.c: Could not allocate keyboard transmit IRQ!\n")
		free_irq(IRQ_KEYBOARDRX, NULL);
		return -EBUSY;
	}

	disable_irq(IRQ_KEYBOARDTX);
	(void)IOMD_KARTRX;

	restore_flags(flags);

	register_serio_port(&rpckbd_port);
	printk(KERN_INFO "serio: RiscPC PS/2 kbd port irq %d %d\n", IRQ_KEYBOARDRX, IRQ_KEYBOARDTX);

	return 0;
}

static void __exit rpckbd_exit(void)
{
	free_irq(IRQ_KEYBOARDRX, NULL);
	free_irq(IRQ_KEYBOARDTX, NULL);	

	unregister_serio_port(&rpckbd_port);
}

module_init(rpckbd_init);
module_exit(rpckbd_exit);
