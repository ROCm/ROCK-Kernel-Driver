/*
 * $Id: q40kbd.c,v 1.12 2002/02/02 22:26:44 vojtech Exp $
 *
 *  Copyright (c) 2000-2001 Vojtech Pavlik
 *
 *  Based on the work of:
 *	Richard Zidlicky <Richard.Zidlicky@stud.informatik.uni-erlangen.de>	
 */

/*
 * Q40 PS/2 keyboard controller driver for Linux/m68k
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

#include <asm/keyboard.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/q40_master.h>
#include <asm/irq.h>
#include <asm/q40ints.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("Q40 PS/2 keyboard controller driver");
MODULE_LICENSE("GPL");

static struct serio q40kbd_port =
{
	type:   SERIO_8042,
	write:  NULL,
	name:	"Q40 PS/2 kbd port",
	phys:	"isa0060/serio0",
};

static void q40kbd_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned long flags;

	if (IRQ_KEYB_MASK & master_inb(INTERRUPT_REG))
		if (q40kbd_port.dev)
                         q40kbd_port.dev->interrupt(&q40kbd_port, master_inb(KEYCODE_REG), 0);

	master_outb(-1, KEYBOARD_UNLOCK_REG);
}

void __init q40kbd_init(void)
{
	int maxread = 100;

	/* Get the keyboard controller registers (incomplete decode) */
	request_region(0x60, 16, "q40kbd");

	/* allocate the IRQ */
	request_irq(Q40_IRQ_KEYBOARD, q40kbd_interrupt, 0, "q40kbd", NULL);

	/* flush any pending input. */
	while (maxread-- && (IRQ_KEYB_MASK & master_inb(INTERRUPT_REG)))
		master_inb(KEYCODE_REG);
	
	/* off we go */
	master_outb(-1,KEYBOARD_UNLOCK_REG);
	master_outb(1,KEY_IRQ_ENABLE_REG);

	register_serio_port(&q40kbd_port);
	printk(KERN_INFO "serio: Q40 PS/2 kbd port irq %d\n", Q40_IRQ_KEYBOARD);
}

void __exit q40kbd_exit(void)
{
	unregister_serio_port(&q40kbd_port);
	free_irq(Q40_IRQ_KEYBOARD, NULL);
	release_region(0x60, 16);	
}

module_init(q40kbd_init);
module_exit(q40kbd_exit);
