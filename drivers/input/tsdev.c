/*
 * $Id: tsdev.c,v 1.15 2002/04/10 16:50:19 jsimmons Exp $
 *
 *  Copyright (c) 2001 "Crazy" james Simmons 
 *
 *  Input driver to Touchscreen device driver module.
 *
 *  Sponsored by Transvirtual Technology
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
 * e-mail - mail your message to <jsimmons@transvirtual.com>.
 */

#define TSDEV_MINOR_BASE 	128
#define TSDEV_MINORS		32
#define TSDEV_BUFFER_SIZE	64

#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/major.h>
#include <linux/config.h>
#include <linux/smp_lock.h>
#include <linux/random.h>
#include <linux/time.h>
#include <linux/device.h>
#include <linux/devfs_fs_kernel.h>

#ifndef CONFIG_INPUT_TSDEV_SCREEN_X
#define CONFIG_INPUT_TSDEV_SCREEN_X	240
#endif
#ifndef CONFIG_INPUT_TSDEV_SCREEN_Y
#define CONFIG_INPUT_TSDEV_SCREEN_Y	320
#endif

struct tsdev {
	int exist;
	int open;
	int minor;
	char name[16];
	wait_queue_head_t wait;
	struct list_head list;
	struct input_handle handle;
};

/* From Compaq's Touch Screen Specification version 0.2 (draft) */
typedef struct {
	short pressure;
	short x;
	short y;
	short millisecs;
} TS_EVENT;

struct tsdev_list {
	struct fasync_struct *fasync;
	struct list_head node;
	struct tsdev *tsdev;
	int head, tail;
	int oldx, oldy, pendown;
	TS_EVENT event[TSDEV_BUFFER_SIZE];
};

static struct input_handler tsdev_handler;

static struct tsdev *tsdev_table[TSDEV_MINORS];

static int xres = CONFIG_INPUT_TSDEV_SCREEN_X;
static int yres = CONFIG_INPUT_TSDEV_SCREEN_Y;

static int tsdev_fasync(int fd, struct file *file, int on)
{
	struct tsdev_list *list = file->private_data;
	int retval;

	retval = fasync_helper(fd, file, on, &list->fasync);
	return retval < 0 ? retval : 0;
}

static int tsdev_open(struct inode *inode, struct file *file)
{
	int i = iminor(inode) - TSDEV_MINOR_BASE;
	struct tsdev_list *list;

	if (i >= TSDEV_MINORS || !tsdev_table[i])
		return -ENODEV;

	if (!(list = kmalloc(sizeof(struct tsdev_list), GFP_KERNEL)))
		return -ENOMEM;
	memset(list, 0, sizeof(struct tsdev_list));

	list->tsdev = tsdev_table[i];
	list_add_tail(&list->node, &tsdev_table[i]->list);
	file->private_data = list;

	if (!list->tsdev->open++)
		if (list->tsdev->exist)
			input_open_device(&list->tsdev->handle);
	return 0;
}

static void tsdev_free(struct tsdev *tsdev)
{
	devfs_remove("input/ts%d", tsdev->minor);
	tsdev_table[tsdev->minor] = NULL;
	kfree(tsdev);
}

static int tsdev_release(struct inode *inode, struct file *file)
{
	struct tsdev_list *list = file->private_data;

	tsdev_fasync(-1, file, 0);
	list_del(&list->node);

	if (!--list->tsdev->open) {
		if (list->tsdev->exist)
			input_close_device(&list->tsdev->handle);
		else
			tsdev_free(list->tsdev);
	}
	kfree(list);
	return 0;
}

static ssize_t tsdev_read(struct file *file, char *buffer, size_t count,
			  loff_t * ppos)
{
	struct tsdev_list *list = file->private_data;
	int retval = 0;

	if (list->head == list->tail && list->tsdev->exist && (file->f_flags & O_NONBLOCK))
		return -EAGAIN;

	retval = wait_event_interruptible(list->tsdev->wait,
		(list->head != list->tail) && list->tsdev->exist);

	if (retval)
		return retval;

	if (!list->tsdev->exist)
		return -ENODEV;

	while (list->head != list->tail && retval + sizeof(TS_EVENT) <= count) {
		if (copy_to_user (buffer + retval, list->event + list->tail, sizeof(TS_EVENT)))
			return -EFAULT;
		list->tail = (list->tail + 1) & (TSDEV_BUFFER_SIZE - 1);
		retval += sizeof(TS_EVENT);
	}

	return retval;
}

/* No kernel lock - fine */
static unsigned int tsdev_poll(struct file *file, poll_table * wait)
{
	struct tsdev_list *list = file->private_data;

	poll_wait(file, &list->tsdev->wait, wait);
	if (list->head != list->tail)
		return POLLIN | POLLRDNORM;
	return 0;
}

static int tsdev_ioctl(struct inode *inode, struct file *file,
		       unsigned int cmd, unsigned long arg)
{
/*
	struct tsdev_list *list = file->private_data;
        struct tsdev *evdev = list->tsdev;
        struct input_dev *dev = tsdev->handle.dev;
        int retval;
	
	switch (cmd) {
		case HHEHE:
			return 0;
		case hjff:
			return 0;
		default:
			return 0;
	}
*/
	return -EINVAL;
}

struct file_operations tsdev_fops = {
	.owner =	THIS_MODULE,
	.open =		tsdev_open,
	.release =	tsdev_release,
	.read =		tsdev_read,
	.poll =		tsdev_poll,
	.fasync =	tsdev_fasync,
	.ioctl =	tsdev_ioctl,
};

static void tsdev_event(struct input_handle *handle, unsigned int type,
			unsigned int code, int value)
{
	struct tsdev *tsdev = handle->private;
	struct tsdev_list *list;
	struct timeval time;
	int size;

	list_for_each_entry(list, &tsdev->list, node) {
		switch (type) {
		case EV_ABS:
			switch (code) {
			case ABS_X:
				if (!list->pendown)
					return;
				size = handle->dev->absmax[ABS_X] - handle->dev->absmin[ABS_X];
				if (size > 0)
					list->oldx = ((value - handle->dev->absmin[ABS_X]) * xres / size);
				else
					list->oldx = ((value - handle->dev->absmin[ABS_X]));
				break;
			case ABS_Y:
				if (!list->pendown)
					return;
				size = handle->dev->absmax[ABS_Y] - handle->dev->absmin[ABS_Y];
				if (size > 0)
					list->oldy = ((value - handle->dev->absmin[ABS_Y]) * yres / size);
				else
					list->oldy = ((value - handle->dev->absmin[ABS_Y]));
				break;
			case ABS_PRESSURE:
				list->pendown = ((value > handle->dev-> absmin[ABS_PRESSURE])) ?
				    value - handle->dev->absmin[ABS_PRESSURE] : 0;
				break;
			}
			break;

		case EV_REL:
			switch (code) {
			case REL_X:
				if (!list->pendown)
					return;
				list->oldx += value;
				if (list->oldx < 0)
					list->oldx = 0;
				else if (list->oldx > xres)
					list->oldx = xres;
				break;
			case REL_Y:
				if (!list->pendown)
					return;
				list->oldy += value;
				if (list->oldy < 0)
					list->oldy = 0;
				else if (list->oldy > xres)
					list->oldy = xres;
				break;
			}
			break;

		case EV_KEY:
			if (code == BTN_TOUCH || code == BTN_MOUSE) {
				switch (value) {
				case 0:
					list->pendown = 0;
					break;
				case 1:
					if (!list->pendown)
						list->pendown = 1;
					break;
				case 2:
					return;
				}
			} else
				return;
			break;
		}
		do_gettimeofday(&time);
		list->event[list->head].millisecs = time.tv_usec / 100;
		list->event[list->head].pressure = list->pendown;
		list->event[list->head].x = list->oldx;
		list->event[list->head].y = list->oldy;
		list->head = (list->head + 1) & (TSDEV_BUFFER_SIZE - 1);
		kill_fasync(&list->fasync, SIGIO, POLL_IN);
	}
	wake_up_interruptible(&tsdev->wait);
}

static struct input_handle *tsdev_connect(struct input_handler *handler,
					  struct input_dev *dev,
					  struct input_device_id *id)
{
	struct tsdev *tsdev;
	int minor;

	for (minor = 0; minor < TSDEV_MINORS && tsdev_table[minor];
	     minor++);
	if (minor == TSDEV_MINORS) {
		printk(KERN_ERR
		       "tsdev: You have way too many touchscreens\n");
		return NULL;
	}

	if (!(tsdev = kmalloc(sizeof(struct tsdev), GFP_KERNEL)))
		return NULL;
	memset(tsdev, 0, sizeof(struct tsdev));

	INIT_LIST_HEAD(&tsdev->list);
	init_waitqueue_head(&tsdev->wait);

	sprintf(tsdev->name, "ts%d", minor);

	tsdev->exist = 1;
	tsdev->minor = minor;
	tsdev->handle.dev = dev;
	tsdev->handle.name = tsdev->name;
	tsdev->handle.handler = handler;
	tsdev->handle.private = tsdev;

	tsdev_table[minor] = tsdev;
	
	devfs_mk_cdev(MKDEV(INPUT_MAJOR, TSDEV_MINOR_BASE + minor),
			S_IFCHR|S_IRUGO|S_IWUSR, "input/ts%d", minor);

	return &tsdev->handle;
}

static void tsdev_disconnect(struct input_handle *handle)
{
	struct tsdev *tsdev = handle->private;

	tsdev->exist = 0;

	if (tsdev->open) {
		input_close_device(handle);
		wake_up_interruptible(&tsdev->wait);
	} else
		tsdev_free(tsdev);
}

static struct input_device_id tsdev_ids[] = {
	{
	      .flags	= INPUT_DEVICE_ID_MATCH_EVBIT | INPUT_DEVICE_ID_MATCH_KEYBIT | INPUT_DEVICE_ID_MATCH_RELBIT,
	      .evbit	= { BIT(EV_KEY) | BIT(EV_REL) },
	      .keybit	= { [LONG(BTN_LEFT)] = BIT(BTN_LEFT) },
	      .relbit	= { BIT(REL_X) | BIT(REL_Y) },
	 },/* A mouse like device, at least one button, two relative axes */

	{
	      .flags	= INPUT_DEVICE_ID_MATCH_EVBIT | INPUT_DEVICE_ID_MATCH_KEYBIT | INPUT_DEVICE_ID_MATCH_ABSBIT,
	      .evbit	= { BIT(EV_KEY) | BIT(EV_ABS) },
	      .keybit	= { [LONG(BTN_TOUCH)] = BIT(BTN_TOUCH) },
	      .absbit	= { BIT(ABS_X) | BIT(ABS_Y) },
	 },/* A tablet like device, at least touch detection, two absolute axes */

	{},/* Terminating entry */
};

MODULE_DEVICE_TABLE(input, tsdev_ids);

static struct input_handler tsdev_handler = {
	.event =	tsdev_event,
	.connect =	tsdev_connect,
	.disconnect =	tsdev_disconnect,
	.fops =		&tsdev_fops,
	.minor =	TSDEV_MINOR_BASE,
	.name =		"tsdev",
	.id_table =	tsdev_ids,
};

static int __init tsdev_init(void)
{
	input_register_handler(&tsdev_handler);
	printk(KERN_INFO "ts: Compaq touchscreen protocol output\n");
	return 0;
}

static void __exit tsdev_exit(void)
{
	input_unregister_handler(&tsdev_handler);
}

module_init(tsdev_init);
module_exit(tsdev_exit);

MODULE_AUTHOR("James Simmons <jsimmons@transvirtual.com>");
MODULE_DESCRIPTION("Input driver to touchscreen converter");
MODULE_PARM(xres, "i");
MODULE_PARM_DESC(xres, "Horizontal screen resolution");
MODULE_PARM(yres, "i");
MODULE_PARM_DESC(yres, "Vertical screen resolution");
