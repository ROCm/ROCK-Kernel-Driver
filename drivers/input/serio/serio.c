/*
 * $Id: serio.c,v 1.15 2002/01/22 21:12:03 vojtech Exp $
 *
 *  Copyright (c) 1999-2001 Vojtech Pavlik
 */

/*
 *  The Serio abstraction module
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

#include <linux/stddef.h>
#include <linux/module.h>
#include <linux/serio.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/completion.h>
#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/suspend.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("Serio abstraction core");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(serio_register_port);
EXPORT_SYMBOL(serio_unregister_port);
EXPORT_SYMBOL(serio_register_device);
EXPORT_SYMBOL(serio_unregister_device);
EXPORT_SYMBOL(serio_open);
EXPORT_SYMBOL(serio_close);
EXPORT_SYMBOL(serio_rescan);

static struct serio *serio_list;
static struct serio_dev *serio_dev;
static int serio_pid;

static void serio_find_dev(struct serio *serio)
{
        struct serio_dev *dev = serio_dev;

        while (dev && !serio->dev) {
		if (dev->connect)
                	dev->connect(serio, dev);
                dev = dev->next;
        }
}

#define SERIO_RESCAN	1

static DECLARE_WAIT_QUEUE_HEAD(serio_wait);
static DECLARE_COMPLETION(serio_exited);

void serio_handle_events(void)
{
	struct serio *serio = serio_list;

	while (serio) {
		if (serio->event & SERIO_RESCAN) {
			if (serio->dev && serio->dev->disconnect)
				serio->dev->disconnect(serio);
			serio_find_dev(serio);
		}

		serio->event = 0;
		serio = serio->next;
	}
}

static int serio_thread(void *nothing)
{
	lock_kernel();
	daemonize();
	strcpy(current->comm, "kseriod");

	do {
		serio_handle_events();
		if (current->flags & PF_FREEZE)
			refrigerator(PF_IOTHREAD);
		interruptible_sleep_on(&serio_wait); 
	} while (!signal_pending(current));

	printk(KERN_DEBUG "serio: kseriod exiting");

	unlock_kernel();
	complete_and_exit(&serio_exited, 0);
}

void serio_rescan(struct serio *serio)
{
	serio->event |= SERIO_RESCAN;
	wake_up(&serio_wait);
}

void serio_interrupt(struct serio *serio, unsigned char data, unsigned int flags)
{       
        if (serio->dev && serio->dev->interrupt) 
                serio->dev->interrupt(serio, data, flags);
	else
		serio_rescan(serio);
}

void serio_register_port(struct serio *serio)
{
	serio->next = serio_list;	
	serio_list = serio;
	serio_find_dev(serio);
}

void serio_unregister_port(struct serio *serio)
{
        struct serio **serioptr = &serio_list;

        while (*serioptr && (*serioptr != serio)) serioptr = &((*serioptr)->next);
        *serioptr = (*serioptr)->next;

	if (serio->dev && serio->dev->disconnect)
		serio->dev->disconnect(serio);
}

void serio_register_device(struct serio_dev *dev)
{
	struct serio *serio = serio_list;

	dev->next = serio_dev;	
	serio_dev = dev;

	while (serio) {
		if (!serio->dev && dev->connect)
			dev->connect(serio, dev);
		serio = serio->next;
	}
}

void serio_unregister_device(struct serio_dev *dev)
{
        struct serio_dev **devptr = &serio_dev;
	struct serio *serio = serio_list;

        while (*devptr && (*devptr != dev)) devptr = &((*devptr)->next);
        *devptr = (*devptr)->next;

	while (serio) {
		if (serio->dev == dev && dev->disconnect)
			dev->disconnect(serio);
		serio_find_dev(serio);
		serio = serio->next;
	}
}

int serio_open(struct serio *serio, struct serio_dev *dev)
{
	if (serio->open(serio))
		return -1;
	serio->dev = dev;
	return 0;
}

void serio_close(struct serio *serio)
{
	serio->close(serio);
	serio->dev = NULL;
}

int serio_init(void)
{
	int pid;

	pid = kernel_thread(serio_thread, NULL,
		CLONE_FS | CLONE_FILES | CLONE_SIGHAND);

	if (!pid) {
		printk(KERN_WARNING "serio: Failed to start kseriod\n");
		return -1;
	}

	serio_pid = pid;

	return 0;
}

void serio_exit(void)
{
	kill_proc(serio_pid, SIGTERM, 1);
	wait_for_completion(&serio_exited);
}

module_init(serio_init);
module_exit(serio_exit);
