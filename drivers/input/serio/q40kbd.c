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
#include <linux/interrupt.h>

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
	.type	= SERIO_8042,
	.name	= "Q40 kbd port",
	.phys	= "Q40",
	.write	= NULL,
};

static irqreturn_t q40kbd_interrupt(int irq, void *dev_id,
				    struct pt_regs *regs)
{
	if (Q40_IRQ_KEYB_MASK & master_inb(INTERRUPT_REG))
		serio_interrupt(&q40kbd_port, master_inb(KEYCODE_REG), 0, regs);

	master_outb(-1, KEYBOARD_UNLOCK_REG);
	return IRQ_HANDLED;
}

static int __init q40kbd_init(void)
{
	int maxread = 100;

	if (!MACH_IS_Q40)
		return -EIO;

	/* allocate the IRQ */
	request_irq(Q40_IRQ_KEYBOARD, q40kbd_interrupt, 0, "q40kbd", NULL);

	/* flush any pending input */
	while (maxread-- && (Q40_IRQ_KEYB_MASK & master_inb(INTERRUPT_REG)))
		master_inb(KEYCODE_REG);

	/* off we go */
	master_outb(-1,KEYBOARD_UNLOCK_REG);
	master_outb(1,KEY_IRQ_ENABLE_REG);

	serio_register_port(&q40kbd_port);
	printk(KERN_INFO "serio: Q40 kbd registered\n");

	return 0;
}

static void __exit q40kbd_exit(void)
{
	master_outb(0,KEY_IRQ_ENABLE_REG);
	master_outb(-1,KEYBOARD_UNLOCK_REG);

	serio_unregister_port(&q40kbd_port);
	free_irq(Q40_IRQ_KEYBOARD, NULL);
}

module_init(q40kbd_init);
module_exit(q40kbd_exit);
