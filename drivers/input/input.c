/*
 * The input core
 *
 * Copyright (c) 1999-2002 Vojtech Pavlik
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/major.h>
#include <linux/pm.h>
#include <linux/proc_fs.h>
#include <linux/kmod.h>
#include <linux/interrupt.h>
#include <linux/poll.h>
#include <linux/device.h>
#include <linux/devfs_fs_kernel.h>

#define INPUT_DEBUG

MODULE_AUTHOR("Vojtech Pavlik <vojtech@suse.cz>");
MODULE_DESCRIPTION("Input core");
MODULE_LICENSE("GPL");

EXPORT_SYMBOL(input_register_device);
EXPORT_SYMBOL(input_unregister_device);
EXPORT_SYMBOL(input_register_handler);
EXPORT_SYMBOL(input_unregister_handler);
EXPORT_SYMBOL(input_grab_device);
EXPORT_SYMBOL(input_release_device);
EXPORT_SYMBOL(input_open_device);
EXPORT_SYMBOL(input_close_device);
EXPORT_SYMBOL(input_accept_process);
EXPORT_SYMBOL(input_flush_device);
EXPORT_SYMBOL(input_event);
EXPORT_SYMBOL(input_class_add_handle);
EXPORT_SYMBOL(input_class_remove_handle);

#define INPUT_DEVICES	256

static LIST_HEAD(input_dev_list);
static LIST_HEAD(input_handler_list);

static struct input_handler *input_table[8];

#ifdef CONFIG_PROC_FS
static struct proc_dir_entry *proc_bus_input_dir;
DECLARE_WAIT_QUEUE_HEAD(input_devices_poll_wait);
static int input_devices_state;
#endif

static inline unsigned int ms_to_jiffies(unsigned int ms)
{
        unsigned int j;
        j = (ms * HZ + 500) / 1000;
        return (j > 0) ? j : 1;
}


void input_event(struct input_dev *dev, unsigned int type, unsigned int code, int value)
{
	struct input_handle *handle;

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

			if (value == 2)
				break;

			change_bit(code, dev->key);

			if (test_bit(EV_REP, dev->evbit) && dev->rep[REP_PERIOD] && dev->timer.data && value) {
				dev->repeat_key = code;
				mod_timer(&dev->timer, jiffies + ms_to_jiffies(dev->rep[REP_DELAY]));
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

			if (code > SND_MAX || !test_bit(code, dev->sndbit))
				return;

			if (dev->event) dev->event(dev, type, code, value);

			break;

		case EV_REP:

			if (code > REP_MAX || value < 0 || dev->rep[code] == value) return;

			dev->rep[code] = value;
			if (dev->event) dev->event(dev, type, code, value);

			break;

		case EV_FF:
			if (dev->event) dev->event(dev, type, code, value);
			break;
	}

	if (type != EV_SYN)
		dev->sync = 0;

	if (dev->grab)
		dev->grab->handler->event(dev->grab, type, code, value);
	else
		list_for_each_entry(handle, &dev->h_list, d_node)
			if (handle->open)
				handle->handler->event(handle, type, code, value);
}

static void input_repeat_key(unsigned long data)
{
	struct input_dev *dev = (void *) data;

	if (!test_bit(dev->repeat_key, dev->key))
		return;

	input_event(dev, EV_KEY, dev->repeat_key, 2);
	input_sync(dev);

	mod_timer(&dev->timer, jiffies + ms_to_jiffies(dev->rep[REP_PERIOD]));
}

int input_accept_process(struct input_handle *handle, struct file *file)
{
	if (handle->dev->accept)
		return handle->dev->accept(handle->dev, file);

	return 0;
}

int input_grab_device(struct input_handle *handle)
{
	if (handle->dev->grab)
		return -EBUSY;

	handle->dev->grab = handle;
	return 0;
}

void input_release_device(struct input_handle *handle)
{
	if (handle->dev->grab == handle)
		handle->dev->grab = NULL;
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
	input_release_device(handle);
	if (handle->dev->pm_dev)
		pm_dev_idle(handle->dev->pm_dev);
	if (handle->dev->close)
		handle->dev->close(handle->dev);
	handle->open--;
}

static void input_link_handle(struct input_handle *handle)
{
	list_add_tail(&handle->d_node, &handle->dev->h_list);
	list_add_tail(&handle->h_node, &handle->handler->h_list);
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

		if (id->flags & INPUT_DEVICE_ID_MATCH_VERSION)
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

static int __input_hotplug(struct input_dev *dev, char **envp, int num_envp,
			   char *buffer, int buffer_size)
{
	char *scratch;
	int i = 0, j;
	scratch = buffer;

	if (!dev)
		return -ENODEV;

	envp[i++] = scratch;
	scratch += sprintf(scratch, "PRODUCT=%x/%x/%x/%x",
		dev->id.bustype, dev->id.vendor, dev->id.product, dev->id.version) + 1;

#ifdef INPUT_DEBUG
	printk(KERN_DEBUG "%s: PRODUCT %x/%x/%x/%x\n", __FUNCTION__,
	       dev->id.bustype, dev->id.vendor, dev->id.product, dev->id.version);
#endif
	if (dev->name) {
		envp[i++] = scratch;
		scratch += sprintf(scratch, "NAME=\"%s\"", dev->name) + 1;
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

	envp[i++] = NULL;

	return 0;
}

int input_hotplug(struct class_device *cdev, char **envp, int num_envp,
		  char *buffer, int buffer_size)
{
	struct input_dev *dev;

	if (!cdev)
		return -ENODEV;
#ifdef INPUT_DEBUG
	printk(KERN_DEBUG "%s: entered for dev %p\n", __FUNCTION__, 
	       &cdev->dev);
#endif

	dev = container_of(cdev,struct input_dev,cdev);

	return __input_hotplug(dev, envp, num_envp, buffer, buffer_size);
}

#endif

#define INPUT_ATTR_BIT_B(bit, max) \
	do { \
		for (i = NBITS(max) - 1; i >= 0; i--) \
			if (dev->bit[i]) break; \
		for (; i >= 0; i--) \
			len += sprintf(buf + len, "%lx ", dev->bit[i]); \
		if (len) len += sprintf(buf + len, "\n"); \
	} while (0)

#define INPUT_ATTR_BIT_B2(bit, max, ev) \
	do { \
		if (test_bit(ev, dev->evbit)) \
			INPUT_ATTR_BIT_B(bit, max); \
	} while (0)


static ssize_t input_class_show_ev(struct class_device *class_dev, char *buf)
{
	struct input_dev *dev = container_of(class_dev, struct input_dev,cdev);
	int i, len = 0;

	INPUT_ATTR_BIT_B(evbit, EV_MAX);
	return len;
}

#define INPUT_CLASS_ATTR_BIT(_name,_bit) \
static ssize_t input_class_show_##_bit(struct class_device *class_dev, \
				       char *buf) \
{ \
	struct input_dev *dev = container_of(class_dev,struct input_dev,cdev); \
        int i, len = 0; \
\
	INPUT_ATTR_BIT_B2(_bit##bit, _name##_MAX, EV_##_name); \
	return len; \
}

INPUT_CLASS_ATTR_BIT(KEY,key)
INPUT_CLASS_ATTR_BIT(REL,rel)
INPUT_CLASS_ATTR_BIT(ABS,abs)
INPUT_CLASS_ATTR_BIT(MSC,msc)
INPUT_CLASS_ATTR_BIT(LED,led)
INPUT_CLASS_ATTR_BIT(SND,snd)
INPUT_CLASS_ATTR_BIT(FF,ff)

static ssize_t input_class_show_phys(struct class_device *class_dev, char *buf)
{
	struct input_dev *dev = container_of(class_dev,struct input_dev,cdev);

	return sprintf(buf, "%s\n", dev->phys ? dev->phys : "(none)" );
}

static ssize_t input_class_show_name(struct class_device *class_dev, char *buf)
{
	struct input_dev *dev = container_of(class_dev,struct input_dev,cdev);

	return sprintf(buf, "%s\n", dev->name ? dev->name : "(none)" );
}

static ssize_t input_class_show_product(struct class_device *class_dev, char *buf)
{
	struct input_dev *dev = container_of(class_dev,struct input_dev,cdev);

	return sprintf(buf, "%x/%x/%x/%x\n", dev->id.bustype, dev->id.vendor, 
		       dev->id.product, dev->id.version);
}

static struct class_device_attribute input_device_class_attrs[] = {
	__ATTR( product, S_IRUGO, input_class_show_product, NULL) ,
	__ATTR( phys, S_IRUGO, input_class_show_phys, NULL ),
	__ATTR( name, S_IRUGO, input_class_show_name, NULL) ,
	__ATTR( ev, S_IRUGO, input_class_show_ev, NULL) ,
	__ATTR( key, S_IRUGO, input_class_show_key, NULL) ,
	__ATTR( rel, S_IRUGO, input_class_show_rel, NULL) ,
	__ATTR( abs, S_IRUGO, input_class_show_abs, NULL) ,
	__ATTR( msc, S_IRUGO, input_class_show_msc, NULL) ,
	__ATTR( led, S_IRUGO, input_class_show_led, NULL) ,
	__ATTR( snd, S_IRUGO, input_class_show_snd, NULL) ,
	__ATTR( ff, S_IRUGO, input_class_show_ff, NULL) ,
	__ATTR_NULL,
};

static void input_device_class_release( struct class_device *class_dev )
{
	put_device(class_dev->dev);
}

static struct class input_device_class = {
	.name =		"input_device",
	.hotplug = 	input_hotplug,
	.release = 	input_device_class_release,
	.class_dev_attrs = input_device_class_attrs,
};

void input_register_device(struct input_dev *dev)
{
	struct input_handle *handle;
	struct input_handler *handler;
	struct input_device_id *id;

	dev->cdev.class = &input_device_class;
	
	dev->cdev.dev = get_device(dev->dev);
	if (class_device_register(&dev->cdev)) {
		put_device(dev->dev);
		return;
	}

	set_bit(EV_SYN, dev->evbit);

	/*
	 * If delay and period are pre-set by the driver, then autorepeating
	 * is handled by the driver itself and we don't do it in input.c.
	 */

	init_timer(&dev->timer);
	if (!dev->rep[REP_DELAY] && !dev->rep[REP_PERIOD]) {
		dev->timer.data = (long) dev;
		dev->timer.function = input_repeat_key;
		dev->rep[REP_DELAY] = 250;
		dev->rep[REP_PERIOD] = 33;
	}

	INIT_LIST_HEAD(&dev->h_list);
	list_add_tail(&dev->node, &input_dev_list);

	list_for_each_entry(handler, &input_handler_list, node)
		if (!handler->blacklist || !input_match_device(handler->blacklist, dev))
			if ((id = input_match_device(handler->id_table, dev)))
				if ((handle = handler->connect(handler, dev, id)))
					input_link_handle(handle);

#ifdef CONFIG_PROC_FS
	input_devices_state++;
	wake_up(&input_devices_poll_wait);
#endif
}

void input_unregister_device(struct input_dev *dev)
{
	struct list_head * node, * next;

	if (!dev) return;

	if (dev->pm_dev)
		pm_unregister(dev->pm_dev);

	del_timer_sync(&dev->timer);

	list_for_each_safe(node, next, &dev->h_list) {
		struct input_handle * handle = to_handle(node);
		list_del_init(&handle->d_node);
		list_del_init(&handle->h_node);
		handle->handler->disconnect(handle);
	}

	list_del_init(&dev->node);

	class_device_unregister(&dev->cdev);

#ifdef CONFIG_PROC_FS
	input_devices_state++;
	wake_up(&input_devices_poll_wait);
#endif
}

void input_register_handler(struct input_handler *handler)
{
	struct input_dev *dev;
	struct input_handle *handle;
	struct input_device_id *id;

	if (!handler) return;

	INIT_LIST_HEAD(&handler->h_list);

	if (handler->fops != NULL)
		input_table[handler->minor >> 5] = handler;

	list_add_tail(&handler->node, &input_handler_list);

	list_for_each_entry(dev, &input_dev_list, node)
		if (!handler->blacklist || !input_match_device(handler->blacklist, dev))
			if ((id = input_match_device(handler->id_table, dev)))
				if ((handle = handler->connect(handler, dev, id)))
					input_link_handle(handle);

#ifdef CONFIG_PROC_FS
	input_devices_state++;
	wake_up(&input_devices_poll_wait);
#endif
}

void input_unregister_handler(struct input_handler *handler)
{
	struct list_head * node, * next;

	list_for_each_safe(node, next, &handler->h_list) {
		struct input_handle * handle = to_handle_h(node);
		list_del_init(&handle->h_node);
		list_del_init(&handle->d_node);
		handler->disconnect(handle);
	}

	list_del_init(&handler->node);

	if (handler->fops != NULL)
		input_table[handler->minor >> 5] = NULL;

#ifdef CONFIG_PROC_FS
	input_devices_state++;
	wake_up(&input_devices_poll_wait);
#endif
}

static int input_open_file(struct inode *inode, struct file *file)
{
	struct input_handler *handler = input_table[iminor(inode) >> 5];
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
	struct input_dev *dev;
	struct input_handle *handle;

	off_t at = 0;
	int i, len, cnt = 0;

	list_for_each_entry(dev, &input_dev_list, node) {

		len = sprintf(buf, "I: Bus=%04x Vendor=%04x Product=%04x Version=%04x\n",
			dev->id.bustype, dev->id.vendor, dev->id.product, dev->id.version);

		len += sprintf(buf + len, "N: Name=\"%s\"\n", dev->name ? dev->name : "");
		len += sprintf(buf + len, "P: Phys=%s\n", dev->phys ? dev->phys : "");
		len += sprintf(buf + len, "H: Handlers=");

		list_for_each_entry(handle, &dev->h_list, d_node)
			len += sprintf(buf + len, "%s ", handle->name);

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
	}

	if (&dev->node == &input_dev_list)
		*eof = 1;

	return (count > cnt) ? cnt : count;
}

static int input_handlers_read(char *buf, char **start, off_t pos, int count, int *eof, void *data)
{
	struct input_handler *handler;

	off_t at = 0;
	int len = 0, cnt = 0;
	int i = 0;

	list_for_each_entry(handler, &input_handler_list, node) {

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
	}
	if (&handler->node == &input_handler_list)
		*eof = 1;

	return (count > cnt) ? cnt : count;
}

static int __init input_proc_init(void)
{
	struct proc_dir_entry *entry;

	proc_bus_input_dir = proc_mkdir("input", proc_bus);
	if (proc_bus_input_dir == NULL)
		return -ENOMEM;
	proc_bus_input_dir->owner = THIS_MODULE;
	entry = create_proc_read_entry("devices", 0, proc_bus_input_dir, input_devices_read, NULL);
	if (entry == NULL) {
		remove_proc_entry("input", proc_bus);
		return -ENOMEM;
	}
	entry->owner = THIS_MODULE;
	entry->proc_fops->poll = input_devices_poll;
	entry = create_proc_read_entry("handlers", 0, proc_bus_input_dir, input_handlers_read, NULL);
	if (entry == NULL) {
		remove_proc_entry("devices", proc_bus_input_dir);
		remove_proc_entry("input", proc_bus);
		return -ENOMEM;
	}
	entry->owner = THIS_MODULE;
	return 0;
}
#else /* !CONFIG_PROC_FS */
static inline int input_proc_init(void) { return 0; }
#endif

static ssize_t input_class_show_dev(struct class_device *class_dev, char *buf)
{
	dev_t dev;
	struct input_handle *handle = class_to_handle(class_dev);
	struct gendev *gdev = to_gendev(handle);

	dev = MKDEV(INPUT_MAJOR,handle->minor_base + gdev->minor);
	return print_dev_t(buf, dev);
}

static struct class_device_attribute input_class_attrs[] = {
	__ATTR( dev, S_IRUGO, input_class_show_dev, NULL),
	__ATTR_NULL,
};

static void input_class_release(struct class_device *cdev)
{
	put_device(cdev->dev);
}

static struct class input_class = {
	.name =		"input",
	.release = 	input_class_release,
	.class_dev_attrs = input_class_attrs,
};

int input_class_add_handle(struct input_handle *handle)
{
	struct gendev *gdev = to_gendev(handle);
	struct device *hdev = NULL;
	int retval;

#ifdef INPUT_DEBUG
	printk(KERN_DEBUG "%s: add handle for %s, minor %d (%p, %p)\n", 
	       __FUNCTION__, gdev->name, gdev->minor,
	       handle, handle->dev);
#endif
	handle->class_dev.class = &input_class;
	if (handle->dev)
		hdev = get_device(handle->dev->dev);

#ifdef INPUT_DEBUG
	if (hdev)
		printk(KERN_DEBUG "%s: bus %p driver %p\n",
		       __FUNCTION__, hdev->bus, hdev->driver);
#endif

	handle->class_dev.dev = hdev;
	snprintf(handle->class_dev.class_id, BUS_ID_SIZE, "%s", gdev->name);
	
	retval = class_device_register(&handle->class_dev);
	if (retval) {
		if (hdev)
			put_device(handle->dev->dev);
		return retval;
	}
	return 0;
}

void input_class_remove_handle(struct input_handle *handle)
{
	class_device_unregister(&handle->class_dev);
}

static int __init input_init(void)
{
	int retval = -ENOMEM;

	retval = class_register(&input_class);
	if (retval)
		return retval;

	retval = class_register(&input_device_class);
	if (retval) {
		class_unregister(&input_class);
		return retval;
	}

	input_proc_init();
	retval = register_chrdev(INPUT_MAJOR, "input", &input_fops);
	if (retval) {
		printk(KERN_ERR "input: unable to register char major %d", INPUT_MAJOR);
		remove_proc_entry("devices", proc_bus_input_dir);
		remove_proc_entry("handlers", proc_bus_input_dir);
		remove_proc_entry("input", proc_bus);
		class_unregister(&input_device_class);
		class_unregister(&input_class);
		return retval;
	}

	retval = devfs_mk_dir("input");
	if (retval) {
		remove_proc_entry("devices", proc_bus_input_dir);
		remove_proc_entry("handlers", proc_bus_input_dir);
		remove_proc_entry("input", proc_bus);
		unregister_chrdev(INPUT_MAJOR, "input");
		class_unregister(&input_device_class);
		class_unregister(&input_class);
	}
	return retval;
}

static void __exit input_exit(void)
{
	remove_proc_entry("devices", proc_bus_input_dir);
	remove_proc_entry("handlers", proc_bus_input_dir);
	remove_proc_entry("input", proc_bus);

	devfs_remove("input");
	unregister_chrdev(INPUT_MAJOR, "input");
	class_unregister(&input_device_class);
	class_unregister(&input_class);
}

subsys_initcall(input_init);
module_exit(input_exit);
