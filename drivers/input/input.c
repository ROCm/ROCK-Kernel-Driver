/*
 * $Id: input.c,v 1.48 2001/12/26 21:08:33 jsimmons Exp $
 *
 *  Copyright (c) 1999-2001 Vojtech Pavlik
 *
 *  The input core
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

#include <linux/init.h>
#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/pm.h>
#include <linux/proc_fs.h>
#include <linux/kmod.h>
#include <linux/interrupt.h>
#include <linux/poll.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("Input core");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(input_register_device);
EXPORT_SYMBOL(input_unregister_device);
EXPORT_SYMBOL(input_register_handler);
EXPORT_SYMBOL(input_unregister_handler);
EXPORT_SYMBOL(input_register_minor);
EXPORT_SYMBOL(input_unregister_minor);
EXPORT_SYMBOL(input_open_device);
EXPORT_SYMBOL(input_close_device);
EXPORT_SYMBOL(input_accept_process);
EXPORT_SYMBOL(input_flush_device);
EXPORT_SYMBOL(input_event);

#define INPUT_MAJOR	13
#define INPUT_DEVICES	256

static struct input_dev *input_dev;
static struct input_handler *input_handler;
static struct input_handler *input_table[8];
static devfs_handle_t input_devfs_handle;

#ifdef CONFIG_PROC_FS
static struct proc_dir_entry *proc_bus_input_dir;
DECLARE_WAIT_QUEUE_HEAD(input_devices_poll_wait);
static int input_devices_state;
#endif

void input_event(struct input_dev *dev, unsigned int type, unsigned int code, int value)
{
	struct input_handle *handle = dev->handle;

	if (dev->pm_dev)
		pm_access(dev->pm_dev);

	if (type > EV_MAX || !test_bit(type, dev->evbit))
		return;

	add_mouse_randomness((type << 4) ^ code ^ (code >> 4) ^ value);

	switch (type) {

		case EV_SYN:
			switch (code) {
				case SYN_CONFIG:
					if (dev->event) dev->event(dev, type, code, value);
					break;

				case SYN_REPORT:
					if (dev->sync) return;
					dev->sync = 1;
					break;
			}
			break;

		case EV_KEY:

			if (code > KEY_MAX || !test_bit(code, dev->keybit) || !!test_bit(code, dev->key) == value)
				return;

			if (value == 2) break;

			change_bit(code, dev->key);

			if (test_bit(EV_REP, dev->evbit) && dev->timer.function) {
				if (value) {
					mod_timer(&dev->timer, jiffies + dev->rep[REP_DELAY]);
					dev->repeat_key = code;
					break;
				}
				if (dev->repeat_key == code)
					del_timer(&dev->timer);
			}

			break;
		
		case EV_ABS:

			if (code > ABS_MAX || !test_bit(code, dev->absbit))
				return;

			if (dev->absfuzz[code]) {
				if ((value > dev->abs[code] - (dev->absfuzz[code] >> 1)) &&
				    (value < dev->abs[code] + (dev->absfuzz[code] >> 1)))
					return;

				if ((value > dev->abs[code] - dev->absfuzz[code]) &&
				    (value < dev->abs[code] + dev->absfuzz[code]))
					value = (dev->abs[code] * 3 + value) >> 2;

				if ((value > dev->abs[code] - (dev->absfuzz[code] << 1)) &&
				    (value < dev->abs[code] + (dev->absfuzz[code] << 1)))
					value = (dev->abs[code] + value) >> 1;
			}

			if (dev->abs[code] == value)
				return;

			dev->abs[code] = value;
			break;

		case EV_REL:

			if (code > REL_MAX || !test_bit(code, dev->relbit) || (value == 0))
				return;

			break;

		case EV_MSC:

			if (code > MSC_MAX || !test_bit(code, dev->mscbit))
				return;

			if (dev->event) dev->event(dev, type, code, value);	
	
			break;

		case EV_LED:
	
			if (code > LED_MAX || !test_bit(code, dev->ledbit) || !!test_bit(code, dev->led) == value)
				return;

			change_bit(code, dev->led);
			if (dev->event) dev->event(dev, type, code, value);	
	
			break;

		case EV_SND:
	
			if (code > SND_MAX || !test_bit(code, dev->sndbit) || !!test_bit(code, dev->snd) == value)
				return;

			change_bit(code, dev->snd);
			if (dev->event) dev->event(dev, type, code, value);	
	
			break;

		case EV_REP:

			if (code > REP_MAX || dev->rep[code] == value) return;

			dev->rep[code] = value;
			if (dev->event) dev->event(dev, type, code, value);

			break;

		case EV_FF:
			if (dev->event) dev->event(dev, type, code, value);
			break;
	}

	if (type != EV_SYN) 
		dev->sync = 0;

	while (handle) {
		if (handle->open)
			handle->handler->event(handle, type, code, value);
		handle = handle->dnext;
	}
}

static void input_repeat_key(unsigned long data)
{
	struct input_dev *dev = (void *) data;
	input_event(dev, EV_KEY, dev->repeat_key, 2);
	input_sync(dev);
	mod_timer(&dev->timer, jiffies + dev->rep[REP_PERIOD]);
}

int input_accept_process(struct input_handle *handle, struct file *file)
{
	if (handle->dev->accept)
		return handle->dev->accept(handle->dev, file);

	return 0;
}

int input_open_device(struct input_handle *handle)
{
	if (handle->dev->pm_dev)
		pm_access(handle->dev->pm_dev);
	handle->open++;
	if (handle->dev->open)
		return handle->dev->open(handle->dev);
	return 0;
}

int input_flush_device(struct input_handle* handle, struct file* file)
{
	if (handle->dev->flush)
		return handle->dev->flush(handle->dev, file);

	return 0;
}

void input_close_device(struct input_handle *handle)
{
	if (handle->dev->pm_dev)
		pm_dev_idle(handle->dev->pm_dev);
	if (handle->dev->close)
		handle->dev->close(handle->dev);
	handle->open--;
}

static void input_link_handle(struct input_handle *handle)
{
	handle->dnext = handle->dev->handle;
	handle->hnext = handle->handler->handle;
	handle->dev->handle = handle;
	handle->handler->handle = handle;
}

/**
 *     input_find_and_remove - Find and remove node
 *
 *     @type:          data type
 *     @initval:       initial value
 *     @targ:          node to find
 *     @next:          next node in the list
 *
 *     Searches the linked list for the target node @targ. If the node
 *     is found, it is removed from the list.
 *
 *     If the node is not found, the end of the list will be hit,
 *     indicating that it wasn't in the list to begin with.
 *
 *     Returns nothing.
 */
#define input_find_and_remove(type, initval, targ, next)		\
	do {								\
		type **ptr;						\
		for (ptr = &initval; *ptr; ptr = &((*ptr)->next))	\
			if (*ptr == targ) break;			\
		if (*ptr) *ptr = (*ptr)->next;				\
	} while (0)

static void input_unlink_handle(struct input_handle *handle)
{
	input_find_and_remove(struct input_handle, handle->dev->handle, handle, dnext);
        input_find_and_remove(struct input_handle, handle->handler->handle, handle, hnext);
}

#define MATCH_BIT(bit, max) \
		for (i = 0; i < NBITS(max); i++) \
			if ((id->bit[i] & dev->bit[i]) != id->bit[i]) \
				break; \
		if (i != NBITS(max)) \
			continue;

static struct input_device_id *input_match_device(struct input_device_id *id, struct input_dev *dev)
{
	int i;

	for (; id->flags || id->driver_info; id++) {

		if (id->flags & INPUT_DEVICE_ID_MATCH_BUS)
			if (id->id.bustype != dev->id.bustype)
				continue;

		if (id->flags & INPUT_DEVICE_ID_MATCH_VENDOR)
			if (id->id.vendor != dev->id.vendor)
				continue;
	
		if (id->flags & INPUT_DEVICE_ID_MATCH_PRODUCT)
			if (id->id.product != dev->id.product)
				continue;
		
		if (id->flags & INPUT_DEVICE_ID_MATCH_BUS)
			if (id->id.version != dev->id.version)
				continue;

		MATCH_BIT(evbit,  EV_MAX);
		MATCH_BIT(keybit, KEY_MAX);
		MATCH_BIT(relbit, REL_MAX);
		MATCH_BIT(absbit, ABS_MAX);
		MATCH_BIT(mscbit, MSC_MAX);
		MATCH_BIT(ledbit, LED_MAX);
		MATCH_BIT(sndbit, SND_MAX);
		MATCH_BIT(ffbit,  FF_MAX);

		return id;
	}

	return NULL;
}

/*
 * Input hotplugging interface - loading event handlers based on
 * device bitfields.
 */

#ifdef CONFIG_HOTPLUG

/*
 * Input hotplugging invokes what /proc/sys/kernel/hotplug says
 * (normally /sbin/hotplug) when input devices get added or removed.
 *
 * This invokes a user mode policy agent, typically helping to load driver
 * or other modules, configure the device, and more.  Drivers can provide
 * a MODULE_DEVICE_TABLE to help with module loading subtasks.
 *
 */

#define SPRINTF_BIT_A(bit, name, max) \
	do { \
		envp[i++] = scratch; \
		scratch += sprintf(scratch, name); \
		for (j = NBITS(max) - 1; j >= 0; j--) \
			if (dev->bit[j]) break; \
		for (; j >= 0; j--) \
			scratch += sprintf(scratch, "%lx ", dev->bit[j]); \
		scratch++; \
	} while (0)

#define SPRINTF_BIT_A2(bit, name, max, ev) \
	do { \
		if (test_bit(ev, dev->evbit)) \
			SPRINTF_BIT_A(bit, name, max); \
	} while (0)

static void input_call_hotplug(char *verb, struct input_dev *dev)
{
	char *argv[3], **envp, *buf, *scratch;
	int i = 0, j, value;

	if (!hotplug_path[0]) {
		printk(KERN_ERR "input.c: calling hotplug a hotplug agent defined\n");
		return;
	}
	if (in_interrupt()) {
		printk(KERN_ERR "input.c: calling hotplug from interrupt\n");
		return; 
	}
	if (!current->fs->root) {
		printk(KERN_WARNING "input.c: calling hotplug without valid filesystem\n");
		return; 
	}
	if (!(envp = (char **) kmalloc(20 * sizeof(char *), GFP_KERNEL))) {
		printk(KERN_ERR "input.c: not enough memory allocating hotplug environment\n");
		return;
	}
	if (!(buf = kmalloc(1024, GFP_KERNEL))) {
		kfree (envp);
		printk(KERN_ERR "input.c: not enough memory allocating hotplug environment\n");
		return;
	}

	argv[0] = hotplug_path;
	argv[1] = "input";
	argv[2] = 0;

	envp[i++] = "HOME=/";
	envp[i++] = "PATH=/sbin:/bin:/usr/sbin:/usr/bin";

	scratch = buf;

	envp[i++] = scratch;
	scratch += sprintf(scratch, "ACTION=%s", verb) + 1;

	envp[i++] = scratch;
	scratch += sprintf(scratch, "PRODUCT=%x/%x/%x/%x",
		dev->id.bustype, dev->id.vendor, dev->id.product, dev->id.version) + 1; 
	
	if (dev->name) {
		envp[i++] = scratch;
		scratch += sprintf(scratch, "NAME=%s", dev->name) + 1; 
	}

	if (dev->phys) {
		envp[i++] = scratch;
		scratch += sprintf(scratch, "PHYS=%s", dev->phys) + 1; 
	}	

	SPRINTF_BIT_A(evbit, "EV=", EV_MAX);
	SPRINTF_BIT_A2(keybit, "KEY=", KEY_MAX, EV_KEY);
	SPRINTF_BIT_A2(relbit, "REL=", REL_MAX, EV_REL);
	SPRINTF_BIT_A2(absbit, "ABS=", ABS_MAX, EV_ABS);
	SPRINTF_BIT_A2(mscbit, "MSC=", MSC_MAX, EV_MSC);
	SPRINTF_BIT_A2(ledbit, "LED=", LED_MAX, EV_LED);
	SPRINTF_BIT_A2(sndbit, "SND=", SND_MAX, EV_SND);
	SPRINTF_BIT_A2(ffbit,  "FF=",  FF_MAX, EV_FF);

	envp[i++] = 0;

	printk(KERN_DEBUG "input.c: calling %s %s [%s %s %s %s %s]\n",
		argv[0], argv[1], envp[0], envp[1], envp[2], envp[3], envp[4]);

	value = call_usermodehelper(argv [0], argv, envp);

	kfree(buf);
	kfree(envp);

	if (value != 0)
		printk(KERN_WARNING "input.c: hotplug returned %d\n", value);
}

#endif

void input_register_device(struct input_dev *dev)
{
	struct input_handler *handler = input_handler;
	struct input_handle *handle;
	struct input_device_id *id;

/*
 * Add the EV_SYN capability.
 */

	set_bit(EV_SYN, dev->evbit);

/*
 * Initialize repeat timer to default values.
 */

	init_timer(&dev->timer);
	dev->timer.data = (long) dev;
	dev->timer.function = input_repeat_key;
	dev->rep[REP_DELAY] = HZ/4;
	dev->rep[REP_PERIOD] = HZ/33;

/*
 * Add the device.
 */

	dev->next = input_dev;	
	input_dev = dev;

/*
 * Notify handlers.
 */

	while (handler) {
		if ((id = input_match_device(handler->id_table, dev)))
			if ((handle = handler->connect(handler, dev, id)))
				input_link_handle(handle);
		handler = handler->next;
	}

/*
 * Notify the hotplug agent.
 */

#ifdef CONFIG_HOTPLUG
	input_call_hotplug("add", dev);
#endif

/*
 * Notify /proc.
 */

#ifdef CONFIG_PROC_FS
	input_devices_state++;
	wake_up(&input_devices_poll_wait);
#endif
}

void input_unregister_device(struct input_dev *dev)
{
	struct input_handle *handle = dev->handle;
	struct input_handle *dnext;

	if (!dev) return;

/*
 * Turn off power management for the device.
 */
	if (dev->pm_dev)
		pm_unregister(dev->pm_dev);

/*
 * Kill any pending repeat timers.
 */

	del_timer(&dev->timer);

/*
 * Notify handlers.
 */

	while (handle) {
		dnext = handle->dnext;
		input_unlink_handle(handle);
		handle->handler->disconnect(handle);
		handle = dnext;
	}

/*
 * Notify the hotplug agent.
 */

#ifdef CONFIG_HOTPLUG
	input_call_hotplug("remove", dev);
#endif

/*
 * Remove the device.
 */
	input_find_and_remove(struct input_dev, input_dev, dev, next);

/*
 * Notify /proc.
 */

#ifdef CONFIG_PROC_FS
	input_devices_state++;
	wake_up(&input_devices_poll_wait);
#endif
}

void input_register_handler(struct input_handler *handler)
{
	struct input_dev *dev = input_dev;
	struct input_handle *handle;
	struct input_device_id *id;

	if (!handler) return;

/*
 * Add minors if needed.
 */

	if (handler->fops != NULL)
		input_table[handler->minor >> 5] = handler;

/*
 * Add the handler.
 */

	handler->next = input_handler;	
	input_handler = handler;
	
/*
 * Notify it about all existing devices.
 */

	while (dev) {
		if ((id = input_match_device(handler->id_table, dev)))
			if ((handle = handler->connect(handler, dev, id)))
				input_link_handle(handle);
		dev = dev->next;
	}

/*
 * Notify /proc.
 */

#ifdef CONFIG_PROC_FS
	input_devices_state++;
	wake_up(&input_devices_poll_wait);
#endif
}

void input_unregister_handler(struct input_handler *handler)
{
	struct input_handle *handle = handler->handle;
	struct input_handle *hnext;

/*
 * Tell the handler to disconnect from all devices it keeps open.
 */

	while (handle) {
		hnext = handle->hnext;
		input_unlink_handle(handle);
		handler->disconnect(handle);
		handle = hnext;
	}

/*
 * Remove it.
 */
	input_find_and_remove(struct input_handler, input_handler, handler,
				next);

/*
 * Remove minors.
 */
	if (handler->fops != NULL)
		input_table[handler->minor >> 5] = NULL;

/*
 * Notify /proc.
 */

#ifdef CONFIG_PROC_FS
	input_devices_state++;
	wake_up(&input_devices_poll_wait);
#endif
}

static int input_open_file(struct inode *inode, struct file *file)
{
	struct input_handler *handler = input_table[minor(inode->i_rdev) >> 5];
	struct file_operations *old_fops, *new_fops = NULL;
	int err;

	/* No load-on-demand here? */
	if (!handler || !(new_fops = fops_get(handler->fops)))
		return -ENODEV;

	/*
	 * That's _really_ odd. Usually NULL ->open means "nothing special",
	 * not "no device". Oh, well...
	 */
	if (!new_fops->open) {
		fops_put(new_fops);
		return -ENODEV;
	}
	old_fops = file->f_op;
	file->f_op = new_fops;

	err = new_fops->open(inode, file);

	if (err) {
		fops_put(file->f_op);
		file->f_op = fops_get(old_fops);
	}
	fops_put(old_fops);
	return err;
}

static struct file_operations input_fops = {
	.owner = THIS_MODULE,
	.open = input_open_file,
};

devfs_handle_t input_register_minor(char *name, int minor, int minor_base)
{
	char devfs_name[16];
	sprintf(devfs_name, name, minor);
	return devfs_register(input_devfs_handle, devfs_name, DEVFS_FL_DEFAULT, INPUT_MAJOR, minor + minor_base,
		S_IFCHR | S_IRUGO | S_IWUSR, &input_fops, NULL);
}

void input_unregister_minor(devfs_handle_t handle)
{
	devfs_unregister(handle);
}

/*
 * ProcFS interface for the input drivers.
 */

#ifdef CONFIG_PROC_FS

#define SPRINTF_BIT_B(bit, name, max) \
	do { \
		len += sprintf(buf + len, "B: %s", name); \
		for (i = NBITS(max) - 1; i >= 0; i--) \
			if (dev->bit[i]) break; \
		for (; i >= 0; i--) \
			len += sprintf(buf + len, "%lx ", dev->bit[i]); \
		len += sprintf(buf + len, "\n"); \
	} while (0)

#define SPRINTF_BIT_B2(bit, name, max, ev) \
	do { \
		if (test_bit(ev, dev->evbit)) \
			SPRINTF_BIT_B(bit, name, max); \
	} while (0)


static unsigned int input_devices_poll(struct file *file, poll_table *wait)
{
	int state = input_devices_state;
	poll_wait(file, &input_devices_poll_wait, wait);
	if (state != input_devices_state)
		return POLLIN | POLLRDNORM;
	return 0;
}

static int input_devices_read(char *buf, char **start, off_t pos, int count, int *eof, void *data)
{
	struct input_dev *dev = input_dev;
	struct input_handle *handle;

	off_t at = 0;
	int i, len, cnt = 0;

	while (dev) {

		len = sprintf(buf, "I: Bus=%04x Vendor=%04x Product=%04x Version=%04x\n",
			dev->id.bustype, dev->id.vendor, dev->id.product, dev->id.version);

		len += sprintf(buf + len, "N: Name=\"%s\"\n", dev->name ? dev->name : "");
		len += sprintf(buf + len, "P: Phys=%s\n", dev->phys ? dev->phys : "");
		len += sprintf(buf + len, "D: Drivers=");

		handle = dev->handle;

		while (handle) {
			len += sprintf(buf + len, "%s ", handle->name);
			handle = handle->dnext;
		}

		len += sprintf(buf + len, "\n");

		SPRINTF_BIT_B(evbit, "EV=", EV_MAX);
		SPRINTF_BIT_B2(keybit, "KEY=", KEY_MAX, EV_KEY);
		SPRINTF_BIT_B2(relbit, "REL=", REL_MAX, EV_REL);
		SPRINTF_BIT_B2(absbit, "ABS=", ABS_MAX, EV_ABS);
		SPRINTF_BIT_B2(mscbit, "MSC=", MSC_MAX, EV_MSC);
		SPRINTF_BIT_B2(ledbit, "LED=", LED_MAX, EV_LED);
		SPRINTF_BIT_B2(sndbit, "SND=", SND_MAX, EV_SND);
		SPRINTF_BIT_B2(ffbit,  "FF=",  FF_MAX, EV_FF);

		len += sprintf(buf + len, "\n");

		at += len;

		if (at >= pos) {
			if (!*start) {
				*start = buf + (pos - (at - len));
				cnt = at - pos;
			} else  cnt += len;
			buf += len;
			if (cnt >= count)
				break;
		}

		dev = dev->next;
	}

	if (!dev) *eof = 1;

	return (count > cnt) ? cnt : count;
}

static int input_handlers_read(char *buf, char **start, off_t pos, int count, int *eof, void *data)
{
	struct input_handler *handler = input_handler;

	off_t at = 0;
	int len = 0, cnt = 0;
	int i = 0;

	while (handler) {

		if (handler->fops)
			len = sprintf(buf, "N: Number=%d Name=%s Minor=%d\n",
				i++, handler->name, handler->minor);
		else
			len = sprintf(buf, "N: Number=%d Name=%s\n",
				i++, handler->name);

		at += len;

		if (at >= pos) {
			if (!*start) {
				*start = buf + (pos - (at - len));
				cnt = at - pos;
			} else  cnt += len;
			buf += len;
			if (cnt >= count)
				break;
		}

		handler = handler->next;
	}

	if (!handler) *eof = 1;

	return (count > cnt) ? cnt : count;
}

#endif

static int __init input_init(void)
{
	struct proc_dir_entry *entry;

#ifdef CONFIG_PROC_FS
	proc_bus_input_dir = proc_mkdir("input", proc_bus);
	proc_bus_input_dir->owner = THIS_MODULE;
	entry = create_proc_read_entry("devices", 0, proc_bus_input_dir, input_devices_read, NULL);
	entry->owner = THIS_MODULE;
	entry->proc_fops->poll = input_devices_poll;
	entry = create_proc_read_entry("handlers", 0, proc_bus_input_dir, input_handlers_read, NULL);
	entry->owner = THIS_MODULE;
#endif
	if (devfs_register_chrdev(INPUT_MAJOR, "input", &input_fops)) {
		printk(KERN_ERR "input: unable to register char major %d", INPUT_MAJOR);
		return -EBUSY;
	}

	input_devfs_handle = devfs_mk_dir(NULL, "input", NULL);

	return 0;
}

static void __exit input_exit(void)
{
#ifdef CONFIG_PROC_FS
	remove_proc_entry("devices", proc_bus_input_dir);
	remove_proc_entry("handlers", proc_bus_input_dir);
	remove_proc_entry("input", proc_bus);
#endif
	devfs_unregister(input_devfs_handle);
        if (devfs_unregister_chrdev(INPUT_MAJOR, "input"))
                printk(KERN_ERR "input: can't unregister char major %d", INPUT_MAJOR);
}

module_init(input_init);
module_exit(input_exit);
