/*
 *  i8042 keyboard and mouse controller driver for Linux
 *
 *  Copyright (c) 1999-2002 Vojtech Pavlik
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
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
MODULE_PARM(i8042_restore_ctr, "1i");

static int i8042_noaux;
static int i8042_unlock;
static int i8042_reset;
static int i8042_direct;
static int i8042_restore_ctr;

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
struct timer_list i8042_timer;

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
	while ((~i8042_read_status() & I8042_STR_OBF) && (i < I8042_CTL_TIMEOUT)) {
		udelay(50);
		i++;
	}
	return -(i == I8042_CTL_TIMEOUT);
}

static int i8042_wait_write(void)
{
	int i = 0;
	while ((i8042_read_status() & I8042_STR_IBF) && (i < I8042_CTL_TIMEOUT)) {
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

	while ((i8042_read_status() & I8042_STR_OBF) && (i++ < I8042_BUFFER_SIZE))
#ifdef I8042_DEBUG_IO
		printk(KERN_DEBUG "i8042.c: %02x <- i8042 (flush, %s) [%d]\n",
			i8042_read_data(), i8042_read_status() & I8042_STR_AUXDATA ? "aux" : "kbd",
			(int) (jiffies - i8042_start));
#else
		i8042_read_data();
#endif

	spin_unlock_irqrestore(&i8042_lock, flags);

	return i;
}

/*
 * i8042_command() executes a command on the i8042. It also sends the input
 * parameter(s) of the commands to it, and receives the output value(s). The
 * parameters are to be stored in the param array, and the output is placed
 * into the same array. The number of the parameters and output values is
 * encoded in bits 8-11 of the command number.
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
		i8042_write_command(command & 0xff);
	}
	
	if (!retval)
		for (i = 0; i < ((command >> 12) & 0xf); i++) {
			if ((retval = i8042_wait_write())) break;
#ifdef I8042_DEBUG_IO
			printk(KERN_DEBUG "i8042.c: %02x -> i8042 (parameter) [%d]\n",
				param[i], (int) (jiffies - i8042_start));
#endif
			i8042_write_data(param[i]);
		}

	if (!retval)
		for (i = 0; i < ((command >> 8) & 0xf); i++) {
			if ((retval = i8042_wait_read())) break;
			if (i8042_read_status() & I8042_STR_AUXDATA) 
				param[i] = ~i8042_read_data();
			else
				param[i] = i8042_read_data();
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
		i8042_write_data(c);
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
 * Here we restore the CTR value if requested. I don't know why, but i8042's in
 * half-AT mode tend to trash their CTR when doing the AUX_SEND command. 
 */

	if (i8042_restore_ctr)
		retval |= i8042_command(&i8042_ctr, I8042_CMD_CTL_WCTR);

/*
 * Make sure the interrupt happens and the character is received even
 * in the case the IRQ isn't wired, so that we can receive further
 * characters later.
 */

	i8042_interrupt(0, NULL, NULL);
	return retval;
}

/*
 * i8042_open() is called when a port is open by the higher layer.
 * It allocates the interrupt and enables in in the chip.
 */

static int i8042_open(struct serio *port)
{
	struct i8042_values *values = port->driver;

	i8042_flush();

	if (request_irq(values->irq, i8042_interrupt, 0, "i8042", NULL)) {
		printk(KERN_ERR "i8042.c: Can't get irq %d for %s, unregistering the port.\n", values->irq, values->name);
		values->exists = 0;
		serio_unregister_port(port);
		return -1;
	}

	i8042_ctr |= values->irqen;

	if (i8042_command(&i8042_ctr, I8042_CMD_CTL_WCTR)) {
		printk(KERN_ERR "i8042.c: Can't write CTR while opening %s.\n", values->name);
		return -1;
	}

	i8042_interrupt(0, NULL, NULL);

	return 0;
}

/*
 * i8042_close() frees the interrupt, so that it can possibly be used
 * by another driver. We never know - if the user doesn't have a mouse,
 * the BIOS could have used the AUX interupt for PCI.
 */

static void i8042_close(struct serio *port)
{
	struct i8042_values *values = port->driver;

	i8042_ctr &= ~values->irqen;

	if (i8042_command(&i8042_ctr, I8042_CMD_CTL_WCTR)) {
		printk(KERN_ERR "i8042.c: Can't write CTR while closing %s.\n", values->name);
		return;
	}

	free_irq(values->irq, NULL);

	i8042_flush();
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
	phys:		I8042_KBD_PHYS_DESC,
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
	phys:		I8042_AUX_PHYS_DESC,
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
	unsigned int dfl;

	spin_lock_irqsave(&i8042_lock, flags);

	while ((str = i8042_read_status()) & I8042_STR_OBF) {

		data = i8042_read_data();
		dfl = ((str & I8042_STR_PARITY) ? SERIO_PARITY : 0) |
		      ((str & I8042_STR_TIMEOUT) ? SERIO_TIMEOUT : 0);

#ifdef I8042_DEBUG_IO
		printk(KERN_DEBUG "i8042.c: %02x <- i8042 (interrupt, %s, %d) [%d]\n",
			data, (str & I8042_STR_AUXDATA) ? "aux" : "kbd", irq, (int) (jiffies - i8042_start));
#endif

		if (i8042_aux_values.exists && (str & I8042_STR_AUXDATA)) {
			serio_interrupt(&i8042_aux_port, data, dfl);
		} else {
			if (i8042_kbd_values.exists) {
				if (!i8042_direct) {
					if (data > 0x7f) {
						if (test_and_clear_bit(data & 0x7f, i8042_unxlate_seen)) {
							serio_interrupt(&i8042_kbd_port, 0xf0, dfl);
							data = i8042_unxlate_table[data & 0x7f];
						}
					} else {
						set_bit(data, i8042_unxlate_seen);
						data = i8042_unxlate_table[data];
					}
				}
				serio_interrupt(&i8042_kbd_port, data, dfl);
			}
		}
	}

	spin_unlock_irqrestore(&i8042_lock, flags);
}

/*
 * i8042_controller init initializes the i8042 controller, and,
 * most importantly, sets it into non-xlated mode if that's
 * desired.
 */
	
static int __init i8042_controller_init(void)
{

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
 * Save the CTR for restoral on unload / reboot.
 */

	if (i8042_command(&i8042_ctr, I8042_CMD_CTL_RCTR)) {
		printk(KERN_ERR "i8042.c: Can't read CTR while initializing i8042.\n");
		return -1;
	}

	i8042_initial_ctr = i8042_ctr;

/*
 * Disable the keyboard interface and interrupt. 
 */

	i8042_ctr |= I8042_CTR_KBDDIS;
	i8042_ctr &= ~I8042_CTR_KBDINT;

/*
 * Handle keylock.
 */

	if (~i8042_read_status() & I8042_STR_KEYLOCK) {

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

	i8042_flush();

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

/*
 * Check if AUX irq is available. If it isn't, then there is no point
 * in trying to detect AUX presence.
 */

	if (request_irq(values->irq, i8042_interrupt, 0, "i8042", NULL))
                return -1;
	free_irq(values->irq, NULL);

/*
 * Get rid of bytes in the queue.
 */

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

	i8042_ctr &= ~values->disable;

	if (i8042_command(&i8042_ctr, I8042_CMD_CTL_WCTR)) {
		printk(KERN_WARNING "i8042.c: Can't write CTR while registering.\n");
		return -1; 
	}

	serio_register_port(port);

	printk(KERN_INFO "serio: i8042 %s port at %#x,%#x irq %d\n",
		values->name, I8042_DATA_REG, I8042_COMMAND_REG, values->irq);

	return 0;
}

static void i8042_timer_func(unsigned long data)
{
	i8042_interrupt(0, NULL, NULL);
	mod_timer(&i8042_timer, jiffies + I8042_POLL_PERIOD);
}

#ifndef MODULE
static int __init i8042_setup_reset(char *str)
{
	i8042_reset = 1;
	return 1;
}
static int __init i8042_setup_noaux(char *str)
{
	i8042_noaux = 1;
	return 1;
}
static int __init i8042_setup_unlock(char *str)
{
	i8042_unlock = 1;
	return 1;
}
static int __init i8042_setup_direct(char *str)
{
	i8042_direct = 1;
	return 1;
}
static int __init i8042_setup_restore_ctr(char *str)
{
	i8042_restore_ctr = 1;
	return 1;
}

__setup("i8042_reset", i8042_setup_reset);
__setup("i8042_noaux", i8042_setup_noaux);
__setup("i8042_unlock", i8042_setup_unlock);
__setup("i8042_direct", i8042_setup_direct);
__setup("i8042_restore_ctr", i8042_setup_restore_ctr);
#endif

/*
 * We need to reset the 8042 back to original mode on system shutdown,
 * because otherwise BIOSes will be confused.
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

#if !defined(__i386__) && !defined(__x86_64__)
	i8042_reset = 1;
#endif

	if (!i8042_platform_init())
		return -EBUSY;

	if (i8042_controller_init())
		return -ENODEV;
		
	if (!i8042_noaux && !i8042_check_aux(&i8042_aux_values, &i8042_aux_port))
		i8042_port_register(&i8042_aux_values, &i8042_aux_port);

	i8042_port_register(&i8042_kbd_values, &i8042_kbd_port);

	i8042_timer.function = i8042_timer_func;
	mod_timer(&i8042_timer, jiffies + I8042_POLL_PERIOD);

	register_reboot_notifier(&i8042_notifier);

	return 0;
}

void __exit i8042_exit(void)
{
	unregister_reboot_notifier(&i8042_notifier);

	del_timer(&i8042_timer);
	
	if (i8042_kbd_values.exists)
		serio_unregister_port(&i8042_kbd_port);

	if (i8042_aux_values.exists)
		serio_unregister_port(&i8042_aux_port);

	i8042_controller_cleanup();

	i8042_platform_exit();
}

module_init(i8042_init);
module_exit(i8042_exit);


