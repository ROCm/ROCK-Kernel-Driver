/*
 * arch/v850/kernel/memcons.c -- Console I/O to a memory buffer
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

#include <linux/kernel.h>
#include <linux/console.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/init.h>

/* If this device is enabled, the linker map should define start and
   end points for its buffer. */
extern char memcons_output[], memcons_output_end;

/* Current offset into the buffer.  */
static unsigned long memcons_offs = 0;

/* Spinlock protecting memcons_offs.  */
static spinlock_t memcons_lock = SPIN_LOCK_UNLOCKED;


static size_t write (const char *buf, size_t len)
{
	int flags;
	char *point;

	spin_lock_irqsave (memcons_lock, flags);

	point = memcons_output + memcons_offs;
	if (point + len >= &memcons_output_end) {
		len = &memcons_output_end - point;
		memcons_offs = 0;
	} else
		memcons_offs += len;

	spin_unlock_irqrestore (memcons_lock, flags);

	memcpy (point, buf, len);

	return len;
}


/*  Low-level console. */

static void memcons_write (struct console *co, const char *buf, unsigned len)
{
	while (len > 0)
		len -= write (buf, len);
}

static kdev_t memcons_device (struct console *co)
{
        return MKDEV (TTY_MAJOR, 64 + co->index);
}

static struct console memcons =
{
    .name	= "memcons",
    .write	= memcons_write,
    .device	= memcons_device,
    .flags	= CON_PRINTBUFFER,
    .index	= -1,
};

void memcons_setup (void)
{
	register_console (&memcons);
	printk (KERN_INFO "Console: static memory buffer (memcons)\n");
}

/* Higher level TTY interface.  */

static struct tty_struct *tty_table[1] = { 0 };
static struct termios *tty_termios[1] = { 0 };
static struct termios *tty_termios_locked[1] = { 0 };
static struct tty_driver tty_driver = { 0 };
static int tty_ref_count = 0;

int memcons_tty_open (struct tty_struct *tty, struct file *filp)
{
	return 0;
}

int memcons_tty_write (struct tty_struct *tty, int from_user,
		       const unsigned char *buf, int len)
{
	return write (buf, len);
}

int memcons_tty_write_room (struct tty_struct *tty)
{
	return &memcons_output_end - (memcons_output + memcons_offs);
}

int memcons_tty_chars_in_buffer (struct tty_struct *tty)
{
	/* We have no buffer.  */
	return 0;
}

int __init memcons_tty_init (void)
{
	tty_driver.name = "memcons";
	tty_driver.major = TTY_MAJOR;
	tty_driver.minor_start = 64;
	tty_driver.num = 1;
	tty_driver.type = TTY_DRIVER_TYPE_SYSCONS;

	tty_driver.refcount = &tty_ref_count;

	tty_driver.table = tty_table;
	tty_driver.termios = tty_termios;
	tty_driver.termios_locked = tty_termios_locked;

	tty_driver.init_termios = tty_std_termios;

	tty_driver.open = memcons_tty_open;
	tty_driver.write = memcons_tty_write;
	tty_driver.write_room = memcons_tty_write_room;
	tty_driver.chars_in_buffer = memcons_tty_chars_in_buffer;

	tty_register_driver (&tty_driver);
}
__initcall (memcons_tty_init);
