/*
 * Copyright (C) 2001 Anton Blanchard <anton@au.ibm.com>, IBM
 * Copyright (C) 2001 Paul Mackerras <paulus@au.ibm.com>, IBM
 *
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/console.h>
#include <linux/major.h>
#include <linux/kernel.h>
#include <linux/sysrq.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/sched.h>
#include <linux/kbd_kern.h>
#include <linux/spinlock.h>
#include <linux/cpumask.h>
#include <asm/uaccess.h>
#include <asm/hvconsole.h>

#define HVC_MAJOR	229
#define HVC_MINOR	0

#define TIMEOUT		((HZ + 99) / 100)

static struct tty_driver *hvc_driver;
static int hvc_count;
static int hvc_kicked;
static wait_queue_head_t hvc_wait_queue;
#ifdef CONFIG_MAGIC_SYSRQ
static int sysrq_pressed;
#endif

#define N_OUTBUF	16
#define N_INBUF		16

#define __ALIGNED__	__attribute__((__aligned__(8)))

/* This driver speaks only in "indexes", i.e. logical consoles starting at 0.
 * The ppc64 backend converts those indexes (e.g. hvc0) to whatever the
 * ultimate "vterm number" that the platform understands. */

struct hvc_struct {
	spinlock_t lock;
	int index;
	struct tty_struct *tty;
	unsigned int count;
	int do_wakeup;
	char outbuf[N_OUTBUF] __ALIGNED__;
	int n_outbuf;
	int irq_requested;
};

struct hvc_struct hvc_struct[MAX_NR_HVC_CONSOLES];

static void hvc_kick(void)
{
	hvc_kicked = 1;
	wake_up_interruptible(&hvc_wait_queue);
}

static irqreturn_t hvc_handle_interrupt(int irq, void *dev_instance, struct pt_regs *regs)
{
	hvc_kick();
	return IRQ_HANDLED;
}

static void hvc_unthrottle(struct tty_struct *tty)
{
	hvc_kick();
}

static int hvc_open(struct tty_struct *tty, struct file * filp)
{
	int line = tty->index;
	struct hvc_struct *hp;
	unsigned long flags;
	int irq = NO_IRQ;

	if (line < 0 || line >= MAX_NR_HVC_CONSOLES)
		return -ENODEV;
	hp = &hvc_struct[line];

	tty->driver_data = hp;
	spin_lock_irqsave(&hp->lock, flags);
	hp->tty = tty;
	hp->count++;
	if (hp->count == 1) {
		irq = hvc_interrupt(hp->index);
		if (irq != NO_IRQ)
			hp->irq_requested = 1;
	}
	spin_unlock_irqrestore(&hp->lock, flags);
	/* XX check error, fallback to non-irq ? */
	if (irq != NO_IRQ)
		request_irq(irq, hvc_handle_interrupt, SA_INTERRUPT, "hvc_console", hp);

	/* Force wakeup of the polling thread */
	hvc_kick();

	return 0;
}

static void hvc_close(struct tty_struct *tty, struct file * filp)
{
	struct hvc_struct *hp = tty->driver_data;
	unsigned long flags;
	int irq = NO_IRQ;

	spin_lock_irqsave(&hp->lock, flags);
	if (tty_hung_up_p(filp))
		goto bail;

	if (--hp->count == 0) {
		hp->tty = NULL;
		if (hp->irq_requested)
			irq = hvc_interrupt(hp->index);
		hp->irq_requested = 0;
	} else if (hp->count < 0)
		printk(KERN_ERR "hvc_close %lu: oops, count is %d\n",
		       hp - hvc_struct, hp->count);
 bail:
	spin_unlock_irqrestore(&hp->lock, flags);
	if (irq != NO_IRQ)
		free_irq(irq, hp);
}

static void hvc_hangup(struct tty_struct *tty)
{
	struct hvc_struct *hp = tty->driver_data;
	unsigned long flags;
	int irq = NO_IRQ;

	spin_lock_irqsave(&hp->lock, flags);
	hp->count = 0;
	hp->tty = NULL;
	if (hp->irq_requested)
		irq = hvc_interrupt(hp->index);
	hp->irq_requested = 0;
	spin_unlock_irqrestore(&hp->lock, flags);
	if (irq != NO_IRQ)
		free_irq(irq, hp);
}

/* called with hp->lock held */
static void hvc_push(struct hvc_struct *hp)
{
	int n;

	n = hvc_arch_put_chars(hp->index, hp->outbuf, hp->n_outbuf);
	if (n <= 0) {
		if (n == 0)
			return;
		/* throw away output on error; this happens when
		   there is no session connected to the vterm. */
		hp->n_outbuf = 0;
	} else
		hp->n_outbuf -= n;
	if (hp->n_outbuf > 0)
		memmove(hp->outbuf, hp->outbuf + n, hp->n_outbuf);
	else
		hp->do_wakeup = 1;
}

static inline int __hvc_write_user(struct hvc_struct *hp,
				   const unsigned char *buf, int count)
{
	char *tbuf, *p;
	int tbsize, rsize, written = 0;
	unsigned long flags;

	tbsize = min(count, (int)PAGE_SIZE);
	if (!(tbuf = kmalloc(tbsize, GFP_KERNEL)))
		return -ENOMEM;

	while ((rsize = count - written) > 0) {
		int wsize;
		if (rsize > tbsize)
			rsize = tbsize;

		p = tbuf;
		rsize -= copy_from_user(p, buf, rsize);
		if (!rsize) {
			if (written == 0)
				written = -EFAULT;
			break;
		}
		buf += rsize;

		spin_lock_irqsave(&hp->lock, flags);

		/* Push pending writes: make some room in buffer */
		if (hp->n_outbuf > 0)
			hvc_push(hp);

		for (wsize = N_OUTBUF - hp->n_outbuf; rsize && wsize;
		     wsize = N_OUTBUF - hp->n_outbuf) {
			if (wsize > rsize)
				wsize = rsize;
			memcpy(hp->outbuf + hp->n_outbuf, p, wsize);
			hp->n_outbuf += wsize;
			hvc_push(hp);
			rsize -= wsize;
			p += wsize;
			written += wsize;
		}
		spin_unlock_irqrestore(&hp->lock, flags);

		if (rsize)
			break;

		if (count < tbsize)
			tbsize = count;
	}

	kfree(tbuf);

	return written;
}

static inline int __hvc_write_kernel(struct hvc_struct *hp,
				   const unsigned char *buf, int count)
{
	unsigned long flags;
	int rsize, written = 0;

	spin_lock_irqsave(&hp->lock, flags);

	/* Push pending writes */
	if (hp->n_outbuf > 0)
		hvc_push(hp);

	while (count > 0 && (rsize = N_OUTBUF - hp->n_outbuf) > 0) {
		if (rsize > count)
			rsize = count;
		memcpy(hp->outbuf + hp->n_outbuf, buf, rsize);
		count -= rsize;
		buf += rsize;
		hp->n_outbuf += rsize;
		written += rsize;
		hvc_push(hp);
	}
	spin_unlock_irqrestore(&hp->lock, flags);

	return written;
}

static int hvc_write(struct tty_struct *tty, int from_user,
		     const unsigned char *buf, int count)
{
	struct hvc_struct *hp = tty->driver_data;
	int written;

	if (from_user)
		written = __hvc_write_user(hp, buf, count);
	else
		written = __hvc_write_kernel(hp, buf, count);

	/* Racy, but harmless, kick thread if there are still pending data */
	if (hp->n_outbuf)
		hvc_kick();

	return written;
}

static int hvc_write_room(struct tty_struct *tty)
{
	struct hvc_struct *hp = tty->driver_data;

	return N_OUTBUF - hp->n_outbuf;
}

static int hvc_chars_in_buffer(struct tty_struct *tty)
{
	struct hvc_struct *hp = tty->driver_data;

	return hp->n_outbuf;
}

#define HVC_POLL_READ	0x00000001
#define HVC_POLL_WRITE	0x00000002
#define HVC_POLL_QUICK	0x00000004

static int hvc_poll(int index)
{
	struct hvc_struct *hp = &hvc_struct[index];
	struct tty_struct *tty;
	int i, n, poll_mask = 0;
	char buf[N_INBUF] __ALIGNED__;
	unsigned long flags;
	int read_total = 0;

	spin_lock_irqsave(&hp->lock, flags);

	/* Push pending writes */
	if (hp->n_outbuf > 0)
		hvc_push(hp);
	/* Reschedule us if still some write pending */
	if (hp->n_outbuf > 0)
		poll_mask |= HVC_POLL_WRITE;

	/* No tty attached, just skip */
	tty = hp->tty;
	if (tty == NULL)
		goto bail;

	/* Now check if we can get data (are we throttled ?) */
	if (test_bit(TTY_THROTTLED, &tty->flags))
		goto throttled;

	/* If we aren't interrupt driven and aren't throttled, we always
	 * request a reschedule
	 */
	if (hvc_interrupt(index) == NO_IRQ)
		poll_mask |= HVC_POLL_READ;

	/* Read data if any */
	for (;;) {
		int count = N_INBUF;
		if (count > (TTY_FLIPBUF_SIZE - tty->flip.count))
			count = TTY_FLIPBUF_SIZE - tty->flip.count;

		/* If flip is full, just reschedule a later read */
		if (count == 0) {
			poll_mask |= HVC_POLL_READ;
			break;
		}
		
		n = hvc_arch_get_chars(index, buf, count);
		if (n <= 0) {
			/* Hangup the tty when disconnected from host */
			if (n == -EPIPE) {
				spin_unlock_irqrestore(&hp->lock, flags);
				tty_hangup(tty);
				spin_lock_irqsave(&hp->lock, flags);
			}
			break;
		}
		for (i = 0; i < n; ++i) {
#ifdef CONFIG_MAGIC_SYSRQ
			/* Handle the SysRq Hack */
			if (buf[i] == '\x0f') {	/* ^O -- should support a sequence */
				sysrq_pressed = 1;
				continue;
			} else if (sysrq_pressed) {
				handle_sysrq(buf[i], NULL, tty);
				sysrq_pressed = 0;
				continue;
			}
#endif /* CONFIG_MAGIC_SYSRQ */
			tty_insert_flip_char(tty, buf[i], 0);
		}

		if (tty->flip.count)
			tty_schedule_flip(tty);

		/* Account the total amount read in one loop, and if above 64 bytes,
		 * we do a quick schedule loop to let the tty grok the data and
		 * eventually throttle us
		 */
		read_total += n;
		if (read_total >= 64) {
			poll_mask |= HVC_POLL_QUICK;
			break;
		}
	}
 throttled:
	/* Wakeup write queue if necessary */
	if (hp->do_wakeup) {
		hp->do_wakeup = 0;
		if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP))
		    && tty->ldisc.write_wakeup)
			(tty->ldisc.write_wakeup)(tty);
		wake_up_interruptible(&tty->write_wait);
	}
 bail:
	spin_unlock_irqrestore(&hp->lock, flags);

	return poll_mask;
}

#if defined(CONFIG_XMON) && defined(CONFIG_SMP)
extern cpumask_t cpus_in_xmon;
#else
static const cpumask_t cpus_in_xmon = CPU_MASK_NONE;
#endif


int khvcd(void *unused)
{
	int i, poll_mask;

	daemonize("khvcd");

	for (;;) {
		wait_queue_t wait = __WAITQUEUE_INITIALIZER(wait,current);

		poll_mask = 0;
		hvc_kicked = 0;
		wmb();
		if (cpus_empty(cpus_in_xmon)) {
			for (i = 0; i < MAX_NR_HVC_CONSOLES; ++i)
				poll_mask |= hvc_poll(i);
		} else
			poll_mask |= HVC_POLL_READ;
		if (hvc_kicked)
			continue;
		if (poll_mask & HVC_POLL_QUICK) {
			yield();
			continue;
		}
		add_wait_queue(&hvc_wait_queue, &wait);
		set_current_state(TASK_INTERRUPTIBLE);
		if (!hvc_kicked) {
			if (poll_mask == 0)
				schedule();
			else
				schedule_timeout(TIMEOUT);
		}
		set_current_state(TASK_RUNNING);
		remove_wait_queue(&hvc_wait_queue, &wait);
	}
}

static int hvc_tiocmget(struct tty_struct *tty, struct file *file)
{
	struct hvc_struct *hp = tty->driver_data;
	int ret = -EIO;

	if (!file || !tty_hung_up_p(file)) {
		ret = hvc_arch_tiocmget(hp->index);
	}
	return ret;
}

static int hvc_tiocmset(struct tty_struct *tty, struct file *file,
	unsigned int set, unsigned int clear)
{
	struct hvc_struct *hp = tty->driver_data;
	int ret = -EIO;

	if (!file || !tty_hung_up_p(file)) {
		ret = hvc_arch_tiocmset(hp->index, set, clear);
	}

	return ret;
}

static struct tty_operations hvc_ops = {
	.open = hvc_open,
	.close = hvc_close,
	.write = hvc_write,
	.hangup = hvc_hangup,
	.unthrottle = hvc_unthrottle,
	.write_room = hvc_write_room,
	.chars_in_buffer = hvc_chars_in_buffer,
	.tiocmget = hvc_tiocmget,
	.tiocmset = hvc_tiocmset,
};

int __init hvc_init(void)
{
	init_waitqueue_head(&hvc_wait_queue);

	hvc_driver = alloc_tty_driver(hvc_count);
	if (!hvc_driver)
		return -ENOMEM;

	hvc_driver->owner = THIS_MODULE;
	hvc_driver->devfs_name = "hvc/";
	hvc_driver->driver_name = "hvc";
	hvc_driver->name = "hvc";
	hvc_driver->major = HVC_MAJOR;
	hvc_driver->minor_start = HVC_MINOR;
	hvc_driver->type = TTY_DRIVER_TYPE_SYSTEM;
	hvc_driver->init_termios = tty_std_termios;
	hvc_driver->flags = TTY_DRIVER_REAL_RAW;
	tty_set_operations(hvc_driver, &hvc_ops);

	if (tty_register_driver(hvc_driver))
		panic("Couldn't register hvc console driver\n");

	if (hvc_count > 0)
		kernel_thread(khvcd, NULL, CLONE_KERNEL);
	else
		printk(KERN_WARNING "no virtual consoles found\n");

	return 0;
}
device_initcall(hvc_init);

/***** console (not tty) code: *****/

void hvc_console_print(struct console *co, const char *b, unsigned count)
{
	char c[16] __ALIGNED__;
	unsigned i, n;
	int r, donecr = 0;

	i = n = 0;
	while (count > 0 || i > 0) {
		if (count > 0 && i < sizeof(c)) {
			if (b[n] == '\n' && !donecr) {
				c[i++] = '\r';
				donecr = 1;
			} else {
				c[i++] = b[n++];
				donecr = 0;
				--count;
			}
		} else {
			r = hvc_arch_put_chars(co->index, c, i);
			if (r < 0) {
				/* throw away chars on error */
				i = 0;
			} else if (r > 0) {
				i -= r;
				if (i > 0)
					memmove(c, c+r, i);
			}
		}
	}
}

static struct tty_driver *hvc_console_device(struct console *c, int *index)
{
	*index = c->index;
	return hvc_driver;
}

static int __init hvc_console_setup(struct console *co, char *options)
{
	if (co->index < 0 || co->index >= MAX_NR_HVC_CONSOLES
	    || co->index >= hvc_count)
		return -1;
	return 0;
}

struct console hvc_con_driver = {
	.name		= "hvc",
	.write		= hvc_console_print,
	.device		= hvc_console_device,
	.setup		= hvc_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
};

/* hvc_instantiate - called once per discovered vterm by hvc_arch_find_vterms */
int hvc_instantiate(void)
{
	struct hvc_struct *hvc;

	if (hvc_count >= MAX_NR_HVC_CONSOLES)
		return -1;

	hvc = &hvc_struct[hvc_count];
	hvc->lock = SPIN_LOCK_UNLOCKED;
	hvc->index = hvc_count;

	hvc_count++;

	return 0;
}

static int __init hvc_console_init(void)
{
	hvc_arch_find_vterms(); /* populate hvc_struct[] early */
	register_console(&hvc_con_driver);
	return 0;
}
console_initcall(hvc_console_init);
