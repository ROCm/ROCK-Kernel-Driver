/*
 * $Id: i8042.c,v 1.21 2002/03/01 22:09:27 jsimmons Exp $
 *
 *  Copyright (c) 1999-2001 Vojtech Pavlik
 */

/*
 *  i8042 keyboard and mouse controller driver for Linux
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

#include <asm/io.h>

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/config.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/serio.h>
#include <linux/sched.h>	/* request/free_irq */

#include "i8042.h"

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("i8042 keyboard and mouse controller driver");
MODULE_LICENSE("GPL");

MODULE_PARM(i8042_noaux, "1i");
MODULE_PARM(i8042_unlock, "1i");
MODULE_PARM(i8042_reset, "1i");
MODULE_PARM(i8042_direct, "1i");

static int i8042_noaux;
static int i8042_unlock;
static int i8042_reset;
static int i8042_direct;

spinlock_t i8042_lock = SPIN_LOCK_UNLOCKED;

struct i8042_values {
	int irq;
	unsigned char disable;
	unsigned char irqen;
	unsigned char exists;
	unsigned char *name;
	unsigned char *phys;
};

static struct serio i8042_kbd_port;
static struct serio i8042_aux_port;
static unsigned char i8042_initial_ctr;
static unsigned char i8042_ctr;

#ifdef I8042_DEBUG_IO
static unsigned long i8042_start;
#endif

static unsigned long i8042_unxlate_seen[128 / BITS_PER_LONG];
static unsigned char i8042_unxlate_table[128] = {
	  0,118, 22, 30, 38, 37, 46, 54, 61, 62, 70, 69, 78, 85,102, 13,
	 21, 29, 36, 45, 44, 53, 60, 67, 68, 77, 84, 91, 90, 20, 28, 27,
	 35, 43, 52, 51, 59, 66, 75, 76, 82, 14, 18, 93, 26, 34, 33, 42,
	 50, 49, 58, 65, 73, 74, 89,124, 17, 41, 88,  5,  6,  4, 12,  3,
	 11,  2, 10,  1,  9,119,126,108,117,125,123,107,115,116,121,105,
	114,122,112,113,127, 96, 97,120,  7, 15, 23, 31, 39, 47, 55, 63,
	 71, 79, 86, 94,  8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 87,111,
	 19, 25, 57, 81, 83, 92, 95, 98, 99,100,101,103,104,106,109,110
};

static void i8042_interrupt(int irq, void *dev_id, struct pt_regs *regs);

/*
 * The i8042_wait_read() and i8042_wait_write functions wait for the i8042 to
 * be ready for reading values from it / writing values to it.
 */

static int i8042_wait_read(void)
{
	int i = 0;
	while ((~inb(I8042_STATUS_REG) & I8042_STR_OBF) && (i < I8042_CTL_TIMEOUT)) {
		udelay(50);
		i++;
	}
	return -(i == I8042_CTL_TIMEOUT);
}

static int i8042_wait_write(void)
{
	int i = 0;
	while ((inb(I8042_STATUS_REG) & I8042_STR_IBF) && (i < I8042_CTL_TIMEOUT)) {
		udelay(50);
		i++;
	}
	return -(i == I8042_CTL_TIMEOUT);
}

/*
 * i8042_flush() flushes all data that may be in the keyboard and mouse buffers
 * of the i8042 down the toilet.
 */

static int i8042_flush(void)
{
	unsigned long flags;
	int i = 0;

	spin_lock_irqsave(&i8042_lock, flags);

	while ((inb(I8042_STATUS_REG) & I8042_STR_OBF) && (i++ < I8042_BUFFER_SIZE))
#ifdef I8042_DEBUG_IO
		printk(KERN_DEBUG "i8042.c: %02x <- i8042 (flush) [%d]\n",
			inb(I8042_DATA_REG), (int) (jiffies - i8042_start));
#else
		inb(I8042_DATA_REG);
#endif

	spin_unlock_irqrestore(&i8042_lock, flags);

	return i;
}

/*
 * i8042_command() executes a command on the i8042. It also sends the input parameter(s)
 * of the commands to it, and receives the output value(s). The parameters are to be
 * stored in the param array, and the output is placed into the same array. The number
 * of the parameters and output values is encoded in bits 8-11 of the command
 * number.
 */

static int i8042_command(unsigned char *param, int command)
{ 
	unsigned long flags;
	int retval = 0, i = 0;

	spin_lock_irqsave(&i8042_lock, flags);

	retval = i8042_wait_write();
	if (!retval) {
#ifdef I8042_DEBUG_IO
		printk(KERN_DEBUG "i8042.c: %02x -> i8042 (command) [%d]\n",
			command & 0xff, (int) (jiffies - i8042_start));
#endif
		outb(command & 0xff, I8042_COMMAND_REG);
	}
	
	if (!retval)
		for (i = 0; i < ((command >> 12) & 0xf); i++) {
			if ((retval = i8042_wait_write())) break;
#ifdef I8042_DEBUG_IO
			printk(KERN_DEBUG "i8042.c: %02x -> i8042 (parameter) [%d]\n",
				param[i], (int) (jiffies - i8042_start));
#endif
			outb(param[i], I8042_DATA_REG);
		}

	if (!retval)
		for (i = 0; i < ((command >> 8) & 0xf); i++) {
			if ((retval = i8042_wait_read())) break;
			if (inb(I8042_STATUS_REG) & I8042_STR_AUXDATA) 
				param[i] = ~inb(I8042_DATA_REG);
			else
				param[i] = inb(I8042_DATA_REG);
#ifdef I8042_DEBUG_IO
			printk(KERN_DEBUG "i8042.c: %02x <- i8042 (return) [%d]\n",
				param[i], (int) (jiffies - i8042_start));
#endif
		}

	spin_unlock_irqrestore(&i8042_lock, flags);

#ifdef I8042_DEBUG_IO
	if (retval)
		printk(KERN_DEBUG "i8042.c:      -- i8042 (timeout) [%d]\n",
			(int) (jiffies - i8042_start));
#endif

	return retval;
}

/*
 * i8042_kbd_write() sends a byte out through the keyboard interface.
 * It also automatically refreshes the CTR value, since some i8042's
 * trash their CTR after attempting to send data to an nonexistent
 * device.
 */

static int i8042_kbd_write(struct serio *port, unsigned char c)
{
	unsigned long flags;
	int retval = 0;

	spin_lock_irqsave(&i8042_lock, flags);

	if(!(retval = i8042_wait_write())) {
#ifdef I8042_DEBUG_IO
		printk(KERN_DEBUG "i8042.c: %02x -> i8042 (kbd-data) [%d]\n",
			c, (int) (jiffies - i8042_start));
#endif
		outb(c, I8042_DATA_REG);
	}

	spin_unlock_irqrestore(&i8042_lock, flags);

	return retval;
}

/*
 * i8042_aux_write() sends a byte out through the aux interface.
 */

static int i8042_aux_write(struct serio *port, unsigned char c)
{
	int retval;

/*
 * Send the byte out.
 */

	retval  = i8042_command(&c, I8042_CMD_AUX_SEND);

/*
 * Here we restore the CTR value. I don't know why, but i8042's in half-AT
 * mode tend to trash their CTR when doing the AUX_SEND command.
 */

	retval += i8042_command(&i8042_ctr, I8042_CMD_CTL_WCTR);

/*
 * Make sure the interrupt happens and the character is received even
 * in the case the IRQ isn't wired, so that we can receive further
 * characters later.
 */

	i8042_interrupt(0, port, NULL);
	return retval;
}

/*
 * i8042_open() is called when a port is open by the higher layer.
 * It allocates an interrupt and enables the port.
 */

static int i8042_open(struct serio *port)
{
	struct i8042_values *values = port->driver;

/*
 * Allocate the interrupt
 */

	if (request_irq(values->irq, i8042_interrupt, 0, "i8042", NULL)) {
		printk(KERN_ERR "i8042.c: Can't get irq %d for %s\n", values->irq, values->name);
		return -1;
	}

/*
 * Enable the device and its interrupt.
 */

	i8042_ctr |= values->irqen;
	i8042_ctr &= ~values->disable;

	if (i8042_command(&i8042_ctr, I8042_CMD_CTL_WCTR)) {
		printk(KERN_ERR "i8042.c: Can't write CTR while opening %s.\n", values->name);
		return -1;
	}

/*
 * Flush buffers
 */

	i8042_flush();

	return 0;
}

/*
 * i8042_close() frees the interrupt, and disables the interface when the
 * upper layer doesn't need it anymore.
 */

static void i8042_close(struct serio *port)
{
	struct i8042_values *values = port->driver;

/*
 * Disable the device and its interrupt.
 */

	i8042_ctr &= ~values->irqen;
	i8042_ctr |= values->disable;

	if (i8042_command(&i8042_ctr, I8042_CMD_CTL_WCTR)) {
		printk(KERN_ERR "i8042.c: Can't write CTR while closing %s.\n", values->name);
		return;
	}

/*
 * Free the interrupt
 */

	free_irq(values->irq, NULL);
}

/*
 * Structures for registering the devices in the serio.c module.
 */

static struct i8042_values i8042_kbd_values = {
	irq:		I8042_KBD_IRQ,
	irqen:		I8042_CTR_KBDINT,
	disable:	I8042_CTR_KBDDIS,
	name:		"KBD",
	exists:		0,
};

static struct serio i8042_kbd_port =
{
	type:		SERIO_8042,
	write:		i8042_kbd_write,
	open:		i8042_open,
	close:		i8042_close,
	driver:		&i8042_kbd_values,
	name:		"i8042 Kbd Port",
	phys:		"isa0060/serio0",
};

static struct i8042_values i8042_aux_values = {
	irq:		I8042_AUX_IRQ,
	irqen:		I8042_CTR_AUXINT,
	disable:	I8042_CTR_AUXDIS,
	name:		"AUX",
	exists:		0,
};

static struct serio i8042_aux_port =
{
	type:		SERIO_8042,
	write:		i8042_aux_write,
	open:		i8042_open,
	close:		i8042_close,
	driver:		&i8042_aux_values,
	name:		"i8042 Aux Port",
	phys:		"isa0060/serio1",
};

/*
 * i8042_interrupt() is the most important function in this driver -
 * it handles the interrupts from the i8042, and sends incoming bytes
 * to the upper layers.
 */

static void i8042_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned long flags;
	unsigned char str, data;

	spin_lock_irqsave(&i8042_lock, flags);

	while ((str = inb(I8042_STATUS_REG)) & I8042_STR_OBF) {

		data = inb(I8042_DATA_REG);

#ifdef I8042_DEBUG_IO
		printk(KERN_DEBUG "i8042.c: %02x <- i8042 (interrupt-%s) [%d]\n",
			data, (str & I8042_STR_AUXDATA) ? "aux" : "kbd", (int) (jiffies - i8042_start));
#endif

		if (i8042_aux_values.exists && (str & I8042_STR_AUXDATA)) {
			if (i8042_aux_port.dev)
				i8042_aux_port.dev->interrupt(&i8042_aux_port, data, 0);
		} else {
			if (i8042_kbd_values.exists && i8042_kbd_port.dev) {
				if (!i8042_direct) {
					if (data > 0x7f) {
						if (test_and_clear_bit(data & 0x7f, i8042_unxlate_seen)) {
							i8042_kbd_port.dev->interrupt(&i8042_kbd_port, 0xf0, 0);	
							data = i8042_unxlate_table[data & 0x7f];
						}
					} else {
						set_bit(data, i8042_unxlate_seen);
						data = i8042_unxlate_table[data];
					}
				}
				i8042_kbd_port.dev->interrupt(&i8042_kbd_port, data, 0);
			}
		}
	}

	spin_unlock_irqrestore(&i8042_lock, flags);
}

/*
 * i8042_controller init initializes the i8042 controller, and,
 * most importantly, sets it into non-xlated mode.
 */
	
static int __init i8042_controller_init(void)
{

/*
 * Check the i/o region before we touch it.
 */
#if !defined(__i386__) && !defined(__sh__) && !defined(__alpha__) 	
	if (check_region(I8042_DATA_REG, 16)) {
		printk(KERN_ERR "i8042.c: %#x port already in use!\n", I8042_DATA_REG);
		return -1;
	}
#endif

/*
 * Test the i8042. We need to know if it thinks it's working correctly
 * before doing anything else.
 */

	i8042_flush();

	if (i8042_reset) {

		unsigned char param;

		if (i8042_command(&param, I8042_CMD_CTL_TEST)) {
			printk(KERN_ERR "i8042.c: i8042 controller self test timeout.\n");
			return -1;
		}

		if (param != I8042_RET_CTL_TEST) {
			printk(KERN_ERR "i8042.c: i8042 controller selftest failed. (%#x != %#x)\n",
				 param, I8042_RET_CTL_TEST);
			return -1;
		}
	}

/*
 * Read the CTR.
 */

	if (i8042_command(&i8042_ctr, I8042_CMD_CTL_RCTR)) {
		printk(KERN_ERR "i8042.c: Can't read CTR while initializing i8042.\n");
		return -1;
	}

/*
 * Save the CTR for restoral on unload / reboot.
 */

	i8042_initial_ctr = i8042_ctr;

/*
 * Disable both interfaces and their interrupts.
 */

	i8042_ctr |= I8042_CTR_KBDDIS;
	i8042_ctr &= ~I8042_CTR_KBDINT;

/*
 * Handle keylock.
 */

	if (~inb(I8042_STATUS_REG) & I8042_STR_KEYLOCK) {

		if (i8042_unlock) {
			i8042_ctr |= I8042_CTR_IGNKEYLOCK;
		} else {
			printk(KERN_WARNING "i8042.c: Warning: Keylock active.\n");
		}
	}

/*
 * If the chip is configured into nontranslated mode by the BIOS, don't
 * bother enabling translating and just use that happily.
 */

	if (~i8042_ctr & I8042_CTR_XLATE)
		i8042_direct = 1;

/*
 * Set nontranslated mode for the kbd interface if requested by an option.
 * This is vital for a working scancode set 3 support. After this the kbd
 * interface becomes a simple serial in/out, like the aux interface is. If
 * the user doesn't wish this, the driver tries to untranslate the values
 * after the i8042 translates them.
 */

	if (i8042_direct)
		i8042_ctr &= ~I8042_CTR_XLATE;

/*
 * Write CTR back.
 */

	if (i8042_command(&i8042_ctr, I8042_CMD_CTL_WCTR)) {
		printk(KERN_ERR "i8042.c: Can't write CTR while initializing i8042.\n");
		return -1;
	}

	return 0;
}

/*
 * Here we try to reset everything back to a state in which the BIOS will be
 * able to talk to the hardware when rebooting.
 */

void i8042_controller_cleanup(void)
{

/*
 * Reset the controller.
 */

	if (i8042_reset) {
		unsigned char param;

		if (i8042_command(&param, I8042_CMD_CTL_TEST))
			printk(KERN_ERR "i8042.c: i8042 controller reset timeout.\n");
	}

/*
 * Restore the original control register setting.
 */

	i8042_ctr = i8042_initial_ctr;

	if (i8042_command(&i8042_ctr, I8042_CMD_CTL_WCTR))
		printk(KERN_WARNING "i8042.c: Can't restore CTR.\n");

/*
 * Reset anything that is connected to the ports if the ports
 * are enabled in the original config.
 */

	if (i8042_kbd_values.exists)
		i8042_kbd_write(&i8042_kbd_port, 0xff);

	if (i8042_aux_values.exists)
		i8042_aux_write(&i8042_aux_port, 0xff);
}

/*
 * i8042_check_aux() applies as much paranoia as it can at detecting
 * the presence of an AUX interface.
 */

static int __init i8042_check_aux(struct i8042_values *values, struct serio *port)
{
	unsigned char param;

	i8042_flush();

/*
 * Internal loopback test - filters out AT-type i8042's
 */

	param = 0x5a;

	if (i8042_command(&param, I8042_CMD_AUX_LOOP) || param != 0xa5)
		return -1;

/*
 * External connection test - filters out AT-soldered PS/2 i8042's
 */

	if (i8042_command(&param, I8042_CMD_AUX_TEST) || param)
		return -1;

/*
 * Bit assignment test - filters out PS/2 i8042's in AT mode
 */
	
	if (i8042_command(&param, I8042_CMD_AUX_DISABLE))
		return -1;

	if (i8042_command(&param, I8042_CMD_CTL_RCTR) || (~param & I8042_CTR_AUXDIS))
		return -1;	

	if (i8042_command(&param, I8042_CMD_AUX_TEST) || param) {

/*
 * We've got an old AMI i8042 with 'Bad Cache' commands.
 */

		i8042_command(&param, I8042_CMD_AUX_ENABLE);
		return -1;
	}

	if (i8042_command(&param, I8042_CMD_AUX_ENABLE))
		return -1;

	if (i8042_command(&param, I8042_CMD_CTL_RCTR) || (param & I8042_CTR_AUXDIS))
		return -1;	

/*
 * Disable the interface.
 */

	i8042_ctr |= I8042_CTR_AUXDIS;
	i8042_ctr &= ~I8042_CTR_AUXINT;

	if (i8042_command(&i8042_ctr, I8042_CMD_CTL_WCTR))
		return -1;

	return 0;
}

/*
 * i8042_port_register() marks the device as existing,
 * registers it, and reports to the user.
 */

static int __init i8042_port_register(struct i8042_values *values, struct serio *port)
{
	values->exists = 1;
	serio_register_port(port);
	printk(KERN_INFO "serio: i8042 %s port at %#x,%#x irq %d\n",
		values->name, I8042_DATA_REG, I8042_COMMAND_REG, values->irq);

	return 0;
}

/*
 * Module init and cleanup functions.
 */

void __init i8042_setup(char *str, int *ints)
{
	if (!strcmp(str, "i8042_reset=1"))
		i8042_reset = 1;
	if (!strcmp(str, "i8042_noaux=1"))
		i8042_noaux = 1;
	if (!strcmp(str, "i8042_unlock=1"))
		i8042_unlock = 1;
	if (!strcmp(str, "i8042_direct=1"))
		i8042_direct = 1;
}

/*
 * Reset the 8042 back to original mode.
 */
static int i8042_notify_sys(struct notifier_block *this, unsigned long code,
        		    void *unused)
{
        if (code==SYS_DOWN || code==SYS_HALT) 
        	i8042_controller_cleanup();
        return NOTIFY_DONE;
}

static struct notifier_block i8042_notifier=
{
        i8042_notify_sys,
        NULL,
        0
};

int __init i8042_init(void)
{
#ifdef I8042_DEBUG_IO
	i8042_start = jiffies;
#endif

	if (i8042_controller_init())
		return -ENODEV;

	i8042_port_register(&i8042_kbd_values, &i8042_kbd_port);
		
	if (!i8042_noaux && !i8042_check_aux(&i8042_aux_values, &i8042_aux_port))
		i8042_port_register(&i8042_aux_values, &i8042_aux_port);

/* 
 * On ix86 platforms touching the i8042 data register region can do really
 * bad things. Because of this the region is always reserved on ix86 boxes.  
 */
#if !defined(__i386__) && !defined(__sh__) && !defined(__alpha__)
	request_region(I8042_DATA_REG, 16, "i8042");
#endif
	register_reboot_notifier(&i8042_notifier);
	return 0;
}

void __exit i8042_exit(void)
{
	unregister_reboot_notifier(&i8042_notifier);
	
	if (i8042_kbd_values.exists)
		serio_unregister_port(&i8042_kbd_port);

	if (i8042_aux_values.exists)
		serio_unregister_port(&i8042_aux_port);

	i8042_controller_cleanup();
#if !defined(__i386__) && !defined(__sh__) && !defined(__alpha__)
	release_region(I8042_DATA_REG, 16);
#endif
}

module_init(i8042_init);
module_exit(i8042_exit);
