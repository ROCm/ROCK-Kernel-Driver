/*
 *  The Serio abstraction module
 *
 *  Copyright (c) 1999-2004 Vojtech Pavlik
 *  Copyright (c) 2004 Dmitry Torokhov
 *  Copyright (c) 2003 Daniele Bellucci
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
#include <linux/slab.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("Serio abstraction core");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(serio_interrupt);
EXPORT_SYMBOL(serio_register_port);
EXPORT_SYMBOL(serio_register_port_delayed);
EXPORT_SYMBOL(__serio_register_port);
EXPORT_SYMBOL(serio_unregister_port);
EXPORT_SYMBOL(serio_unregister_port_delayed);
EXPORT_SYMBOL(__serio_unregister_port);
EXPORT_SYMBOL(serio_register_device);
EXPORT_SYMBOL(serio_unregister_device);
EXPORT_SYMBOL(serio_open);
EXPORT_SYMBOL(serio_close);
EXPORT_SYMBOL(serio_rescan);
EXPORT_SYMBOL(serio_reconnect);

static DECLARE_MUTEX(serio_sem);				/* protects serio_list and serio_dev_list */
static LIST_HEAD(serio_list);
static LIST_HEAD(serio_dev_list);

/* serio_find_dev() must be called with serio_sem down.  */

static void serio_find_dev(struct serio *serio)
{
	struct serio_dev *dev;

	list_for_each_entry(dev, &serio_dev_list, node) {
		if (serio->dev)
			break;
		if (dev->connect)
			dev->connect(serio, dev);
	}
}

/*
 * Serio event processing.
 */

struct serio_event {
	int type;
	struct serio *serio;
	struct list_head node;
};

enum serio_event_type {
	SERIO_RESCAN,
	SERIO_RECONNECT,
	SERIO_REGISTER_PORT,
	SERIO_UNREGISTER_PORT,
};

static spinlock_t serio_event_lock = SPIN_LOCK_UNLOCKED;	/* protects serio_event_list */
static LIST_HEAD(serio_event_list);
static DECLARE_WAIT_QUEUE_HEAD(serio_wait);
static DECLARE_COMPLETION(serio_exited);
static int serio_pid;

static void serio_queue_event(struct serio *serio, int event_type)
{
	unsigned long flags;
	struct serio_event *event;

	spin_lock_irqsave(&serio_event_lock, flags);

	if ((event = kmalloc(sizeof(struct serio_event), GFP_ATOMIC))) {
		event->type = event_type;
		event->serio = serio;

		list_add_tail(&event->node, &serio_event_list);
		wake_up(&serio_wait);
	}

	spin_unlock_irqrestore(&serio_event_lock, flags);
}

static struct serio_event *serio_get_event(void)
{
	struct serio_event *event;
	struct list_head *node;
	unsigned long flags;

	spin_lock_irqsave(&serio_event_lock, flags);

	if (list_empty(&serio_event_list)) {
		spin_unlock_irqrestore(&serio_event_lock, flags);
		return NULL;
	}

	node = serio_event_list.next;
	event = container_of(node, struct serio_event, node);
	list_del_init(node);

	spin_unlock_irqrestore(&serio_event_lock, flags);

	return event;
}

static void serio_handle_events(void)
{
	struct serio_event *event;

	while ((event = serio_get_event())) {

		down(&serio_sem);

		switch (event->type) {
			case SERIO_REGISTER_PORT :
				__serio_register_port(event->serio);
				break;

			case SERIO_UNREGISTER_PORT :
				__serio_unregister_port(event->serio);
				break;

			case SERIO_RECONNECT :
				if (event->serio->dev && event->serio->dev->reconnect)
					if (event->serio->dev->reconnect(event->serio) == 0)
						break;
				/* reconnect failed - fall through to rescan */

			case SERIO_RESCAN :
				if (event->serio->dev && event->serio->dev->disconnect)
					event->serio->dev->disconnect(event->serio);
				serio_find_dev(event->serio);
				break;
			default:
				break;
		}

		up(&serio_sem);
		kfree(event);
	}
}

static void serio_remove_pending_events(struct serio *serio)
{
	struct list_head *node, *next;
	struct serio_event *event;
	unsigned long flags;

	spin_lock_irqsave(&serio_event_lock, flags);

	list_for_each_safe(node, next, &serio_event_list) {
		event = container_of(node, struct serio_event, node);
		if (event->serio == serio) {
			list_del_init(node);
			kfree(event);
		}
	}

	spin_unlock_irqrestore(&serio_event_lock, flags);
}


static int serio_thread(void *nothing)
{
	lock_kernel();
	daemonize("kseriod");
	allow_signal(SIGTERM);

	do {
		serio_handle_events();
		wait_event_interruptible(serio_wait, !list_empty(&serio_event_list));
		if (current->flags & PF_FREEZE)
			refrigerator(PF_FREEZE);
	} while (!signal_pending(current));

	printk(KERN_DEBUG "serio: kseriod exiting\n");

	unlock_kernel();
	complete_and_exit(&serio_exited, 0);
}


/*
 * Serio port operations
 */

void serio_rescan(struct serio *serio)
{
	serio_queue_event(serio, SERIO_RESCAN);
}

void serio_reconnect(struct serio *serio)
{
	serio_queue_event(serio, SERIO_RECONNECT);
}

void serio_register_port(struct serio *serio)
{
	down(&serio_sem);
	__serio_register_port(serio);
	up(&serio_sem);
}

/*
 * Submits register request to kseriod for subsequent execution.
 * Can be used when it is not obvious whether the serio_sem is
 * taken or not and when delayed execution is feasible.
 */
void serio_register_port_delayed(struct serio *serio)
{
	serio_queue_event(serio, SERIO_REGISTER_PORT);
}

/*
 * Should only be called directly if serio_sem has already been taken,
 * for example when unregistering a serio from other input device's
 * connect() function.
 */
void __serio_register_port(struct serio *serio)
{
	spin_lock_init(&serio->lock);
	list_add_tail(&serio->node, &serio_list);
	serio_find_dev(serio);
}

void serio_unregister_port(struct serio *serio)
{
	down(&serio_sem);
	__serio_unregister_port(serio);
	up(&serio_sem);
}

/*
 * Submits unregister request to kseriod for subsequent execution.
 * Can be used when it is not obvious whether the serio_sem is
 * taken or not and when delayed execution is feasible.
 */
void serio_unregister_port_delayed(struct serio *serio)
{
	serio_queue_event(serio, SERIO_UNREGISTER_PORT);
}

/*
 * Should only be called directly if serio_sem has already been taken,
 * for example when unregistering a serio from other input device's
 * disconnect() function.
 */
void __serio_unregister_port(struct serio *serio)
{
	serio_remove_pending_events(serio);
	list_del_init(&serio->node);
	if (serio->dev && serio->dev->disconnect)
		serio->dev->disconnect(serio);
}

/*
 * Serio device operations
 */

void serio_register_device(struct serio_dev *dev)
{
	struct serio *serio;
	down(&serio_sem);
	list_add_tail(&dev->node, &serio_dev_list);
	list_for_each_entry(serio, &serio_list, node)
		if (!serio->dev && dev->connect)
			dev->connect(serio, dev);
	up(&serio_sem);
}

void serio_unregister_device(struct serio_dev *dev)
{
	struct serio *serio;

	down(&serio_sem);
	list_del_init(&dev->node);

	list_for_each_entry(serio, &serio_list, node) {
		if (serio->dev == dev && dev->disconnect)
			dev->disconnect(serio);
		serio_find_dev(serio);
	}
	up(&serio_sem);
}

/* called from serio_dev->connect/disconnect methods under serio_sem */
int serio_open(struct serio *serio, struct serio_dev *dev)
{
	unsigned long flags;

	spin_lock_irqsave(&serio->lock, flags);
	serio->dev = dev;
	spin_unlock_irqrestore(&serio->lock, flags);
	if (serio->open && serio->open(serio)) {
		spin_lock_irqsave(&serio->lock, flags);
		serio->dev = NULL;
		spin_unlock_irqrestore(&serio->lock, flags);
		return -1;
	}
	return 0;
}

/* called from serio_dev->connect/disconnect methods under serio_sem */
void serio_close(struct serio *serio)
{
	unsigned long flags;

	if (serio->close)
		serio->close(serio);
	spin_lock_irqsave(&serio->lock, flags);
	serio->dev = NULL;
	spin_unlock_irqrestore(&serio->lock, flags);
}

irqreturn_t serio_interrupt(struct serio *serio,
		unsigned char data, unsigned int dfl, struct pt_regs *regs)
{
	unsigned long flags;
	irqreturn_t ret = IRQ_NONE;

	spin_lock_irqsave(&serio->lock, flags);

        if (likely(serio->dev)) {
                ret = serio->dev->interrupt(serio, data, dfl, regs);
	} else {
		if (!dfl) {
			if ((serio->type != SERIO_8042 &&
			     serio->type != SERIO_8042_XL) || (data == 0xaa)) {
				serio_rescan(serio);
				ret = IRQ_HANDLED;
			}
		}
	}

	spin_unlock_irqrestore(&serio->lock, flags);

	return ret;
}

static int __init serio_init(void)
{
	int pid;

	pid = kernel_thread(serio_thread, NULL, CLONE_KERNEL);

	if (!pid) {
		printk(KERN_WARNING "serio: Failed to start kseriod\n");
		return -1;
	}

	serio_pid = pid;

	return 0;
}

static void __exit serio_exit(void)
{
	kill_proc(serio_pid, SIGTERM, 1);
	wait_for_completion(&serio_exited);
}

module_init(serio_init);
module_exit(serio_exit);
