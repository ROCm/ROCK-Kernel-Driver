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
EXPORT_SYMBOL(serio_unregister_port);
EXPORT_SYMBOL(serio_unregister_port_delayed);
EXPORT_SYMBOL(serio_register_driver);
EXPORT_SYMBOL(serio_unregister_driver);
EXPORT_SYMBOL(serio_open);
EXPORT_SYMBOL(serio_close);
EXPORT_SYMBOL(serio_rescan);
EXPORT_SYMBOL(serio_reconnect);

static DECLARE_MUTEX(serio_sem);	/* protects serio_list and serio_diriver_list */
static LIST_HEAD(serio_list);
static LIST_HEAD(serio_driver_list);
static unsigned int serio_no;

struct bus_type serio_bus = {
	.name =	"serio",
};

static void serio_find_driver(struct serio *serio);
static void serio_create_port(struct serio *serio);
static void serio_destroy_port(struct serio *serio);
static void serio_connect_port(struct serio *serio, struct serio_driver *drv);
static void serio_reconnect_port(struct serio *serio);
static void serio_disconnect_port(struct serio *serio);

static int serio_bind_driver(struct serio *serio, struct serio_driver *drv)
{
	get_driver(&drv->driver);

	drv->connect(serio, drv);
	if (serio->drv) {
		down_write(&serio_bus.subsys.rwsem);
		serio->dev.driver = &drv->driver;
		device_bind_driver(&serio->dev);
		up_write(&serio_bus.subsys.rwsem);
		return 1;
	}

	put_driver(&drv->driver);
	return 0;
}

/* serio_find_driver() must be called with serio_sem down.  */
static void serio_find_driver(struct serio *serio)
{
	struct serio_driver *drv;

	list_for_each_entry(drv, &serio_driver_list, node)
		if (serio_bind_driver(serio, drv))
			break;
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
				serio_create_port(event->serio);
				serio_connect_port(event->serio, NULL);
				break;

			case SERIO_UNREGISTER_PORT :
				serio_disconnect_port(event->serio);
				serio_destroy_port(event->serio);
				break;

			case SERIO_RECONNECT :
				serio_reconnect_port(event->serio);
				break;

			case SERIO_RESCAN :
				serio_disconnect_port(event->serio);
				serio_connect_port(event->serio, NULL);
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

static ssize_t serio_show_description(struct device *dev, char *buf)
{
	struct serio *serio = to_serio_port(dev);
	return sprintf(buf, "%s\n", serio->name);
}
static DEVICE_ATTR(description, S_IRUGO, serio_show_description, NULL);

static ssize_t serio_show_driver(struct device *dev, char *buf)
{
	return sprintf(buf, "%s\n", dev->driver ? dev->driver->name : "(none)");
}
static DEVICE_ATTR(driver, S_IRUGO, serio_show_driver, NULL);

static void serio_release_port(struct device *dev)
{
	struct serio *serio = to_serio_port(dev);

	kfree(serio);
	module_put(THIS_MODULE);
}

static void serio_create_port(struct serio *serio)
{
	try_module_get(THIS_MODULE);

	spin_lock_init(&serio->lock);
	list_add_tail(&serio->node, &serio_list);
	snprintf(serio->dev.bus_id, sizeof(serio->dev.bus_id), "serio%d", serio_no++);
	serio->dev.bus = &serio_bus;
	serio->dev.release = serio_release_port;
	if (serio->parent)
		serio->dev.parent = &serio->parent->dev;
	device_register(&serio->dev);
	device_create_file(&serio->dev, &dev_attr_description);
	device_create_file(&serio->dev, &dev_attr_driver);
}

/*
 * serio_destroy_port() completes deregistration process and removes
 * port from the system
 */
static void serio_destroy_port(struct serio *serio)
{
	struct serio_driver *drv = serio->drv;
	unsigned long flags;

	serio_remove_pending_events(serio);
	list_del_init(&serio->node);

	if (drv) {
		drv->disconnect(serio);
		down_write(&serio_bus.subsys.rwsem);
		device_release_driver(&serio->dev);
		up_write(&serio_bus.subsys.rwsem);
		put_driver(&drv->driver);
	}

	if (serio->parent) {
		spin_lock_irqsave(&serio->parent->lock, flags);
		serio->parent->child = NULL;
		spin_unlock_irqrestore(&serio->parent->lock, flags);
	}

	device_unregister(&serio->dev);
}

/*
 * serio_connect_port() tries to bind the port and possible all its
 * children to appropriate drivers. If driver passed in the function will not
 * try otehr drivers when binding parent port.
 */
static void serio_connect_port(struct serio *serio, struct serio_driver *drv)
{
	WARN_ON(serio->drv);
	WARN_ON(serio->child);

	if (drv)
		serio_bind_driver(serio, drv);
	else
		serio_find_driver(serio);

	/* Ok, now bind children, if any */
	while (serio->child) {
		serio = serio->child;

		WARN_ON(serio->drv);
		WARN_ON(serio->child);

		serio_create_port(serio);

		/*
		 * With children we just _prefer_ passed in driver,
		 * but we will try other options in case preferred
		 * is not the one
		 */
		if (!drv || !serio_bind_driver(serio, drv))
			serio_find_driver(serio);
	}
}

/*
 *
 */
static void serio_reconnect_port(struct serio *serio)
{
	do {
		if (!serio->drv || !serio->drv->reconnect || serio->drv->reconnect(serio)) {
			serio_disconnect_port(serio);
			serio_connect_port(serio, NULL);
			/* Ok, old children are now gone, we are done */
			break;
		}
		serio = serio->child;
	} while (serio);
}

/*
 * serio_disconnect_port() unbinds a port from its driver. As a side effect
 * all child ports are unbound and destroyed.
 */
static void serio_disconnect_port(struct serio *serio)
{
	struct serio_driver *drv = serio->drv;
	struct serio *s;

	if (serio->child) {
		/*
		 * Children ports should be disconnected and destroyed
		 * first, staring with the leaf one, since we don't want
		 * to do recursion
		 */
		do {
			s = serio->child;
		} while (s->child);

		while (s != serio) {
			s = s->parent;
			serio_destroy_port(s->child);
		}
	}

	/*
	 * Ok, no children left, now disconnect this port
	 */
	if (drv) {
		drv->disconnect(serio);
		down_write(&serio_bus.subsys.rwsem);
		device_release_driver(&serio->dev);
		up_write(&serio_bus.subsys.rwsem);
		put_driver(&drv->driver);
	}
}

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
	serio_create_port(serio);
	serio_connect_port(serio, NULL);
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

void serio_unregister_port(struct serio *serio)
{
	down(&serio_sem);
	serio_disconnect_port(serio);
	serio_destroy_port(serio);
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
 * Serio driver operations
 */

static ssize_t serio_driver_show_description(struct device_driver *drv, char *buf)
{
	struct serio_driver *driver = to_serio_driver(drv);
	return sprintf(buf, "%s\n", driver->description ? driver->description : "(none)");
}
static DRIVER_ATTR(description, S_IRUGO, serio_driver_show_description, NULL);

void serio_register_driver(struct serio_driver *drv)
{
	struct serio *serio;

	down(&serio_sem);

	list_add_tail(&drv->node, &serio_driver_list);

	drv->driver.bus = &serio_bus;
	driver_register(&drv->driver);
	driver_create_file(&drv->driver, &driver_attr_description);

start_over:
	list_for_each_entry(serio, &serio_list, node) {
		if (!serio->drv) {
			serio_connect_port(serio, drv);
			/*
			 * if new child appeared then the list is changed,
			 * we need to start over
			 */
			if (serio->child)
				goto start_over;
		}
	}

	up(&serio_sem);
}

void serio_unregister_driver(struct serio_driver *drv)
{
	struct serio *serio;

	down(&serio_sem);

	list_del_init(&drv->node);

start_over:
	list_for_each_entry(serio, &serio_list, node) {
		if (serio->drv == drv) {
			serio_disconnect_port(serio);
			serio_connect_port(serio, NULL);
			/* we could've deleted some ports, restart */
			goto start_over;
		}
	}

	driver_unregister(&drv->driver);

	up(&serio_sem);
}

/* called from serio_driver->connect/disconnect methods under serio_sem */
int serio_open(struct serio *serio, struct serio_driver *drv)
{
	unsigned long flags;

	spin_lock_irqsave(&serio->lock, flags);
	serio->drv = drv;
	spin_unlock_irqrestore(&serio->lock, flags);
	if (serio->open && serio->open(serio)) {
		spin_lock_irqsave(&serio->lock, flags);
		serio->drv = NULL;
		spin_unlock_irqrestore(&serio->lock, flags);
		return -1;
	}
	return 0;
}

/* called from serio_driver->connect/disconnect methods under serio_sem */
void serio_close(struct serio *serio)
{
	unsigned long flags;

	if (serio->close)
		serio->close(serio);
	spin_lock_irqsave(&serio->lock, flags);
	serio->drv = NULL;
	spin_unlock_irqrestore(&serio->lock, flags);
}

irqreturn_t serio_interrupt(struct serio *serio,
		unsigned char data, unsigned int dfl, struct pt_regs *regs)
{
	unsigned long flags;
	irqreturn_t ret = IRQ_NONE;

	spin_lock_irqsave(&serio->lock, flags);

        if (likely(serio->drv)) {
                ret = serio->drv->interrupt(serio, data, dfl, regs);
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
	if (!(serio_pid = kernel_thread(serio_thread, NULL, CLONE_KERNEL))) {
		printk(KERN_WARNING "serio: Failed to start kseriod\n");
		return -1;
	}

	bus_register(&serio_bus);

	return 0;
}

static void __exit serio_exit(void)
{
	bus_unregister(&serio_bus);
	kill_proc(serio_pid, SIGTERM, 1);
	wait_for_completion(&serio_exited);
}

module_init(serio_init);
module_exit(serio_exit);
