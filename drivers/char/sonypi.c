/*
 * Sony Programmable I/O Control Device driver for VAIO
 *
 * Copyright (C) 2001-2004 Stelian Pop <stelian@popies.net>
 *
 * Copyright (C) 2001-2002 Alcôve <www.alcove.com>
 *
 * Copyright (C) 2001 Michael Ashley <m.ashley@unsw.edu.au>
 *
 * Copyright (C) 2001 Junichi Morita <jun1m@mars.dti.ne.jp>
 *
 * Copyright (C) 2000 Takaya Kinjo <t-kinjo@tc4.so-net.ne.jp>
 *
 * Copyright (C) 2000 Andrew Tridgell <tridge@valinux.com>
 *
 * Earlier work by Werner Almesberger, Paul `Rusty' Russell and Paul Mackerras.
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#include <linux/poll.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/err.h>

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/system.h>

#include "sonypi.h"
#include <linux/sonypi.h>

MODULE_AUTHOR("Stelian Pop <stelian@popies.net>");
MODULE_DESCRIPTION("Sony Programmable I/O Control Device driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(SONYPI_DRIVER_VERSION);

static int minor = -1;
module_param(minor, int, 0);
MODULE_PARM_DESC(minor,
		 "minor number of the misc device, default is -1 (automatic)");

static int verbose;		/* = 0 */
module_param(verbose, int, 0644);
MODULE_PARM_DESC(verbose, "be verbose, default is 0 (no)");

static int fnkeyinit;		/* = 0 */
module_param(fnkeyinit, int, 0444);
MODULE_PARM_DESC(fnkeyinit,
		 "set this if your Fn keys do not generate any event");

static int camera;		/* = 0 */
module_param(camera, int, 0444);
MODULE_PARM_DESC(camera,
		 "set this if you have a MotionEye camera (PictureBook series)");

static int compat;		/* = 0 */
module_param(compat, int, 0444);
MODULE_PARM_DESC(compat,
		 "set this if you want to enable backward compatibility mode");

static unsigned long mask = 0xffffffff;
module_param(mask, ulong, 0644);
MODULE_PARM_DESC(mask,
		 "set this to the mask of event you want to enable (see doc)");

static int useinput = 1;
module_param(useinput, int, 0444);
MODULE_PARM_DESC(useinput,
		 "set this if you would like sonypi to feed events to the input subsystem");

static struct sonypi_device sonypi_device;

static int sonypi_ec_write(u8 addr, u8 value)
{
#ifdef CONFIG_ACPI_EC
	if (SONYPI_ACPI_ACTIVE)
		return ec_write(addr, value);
#endif
	wait_on_command(1, inb_p(SONYPI_CST_IOPORT) & 3, ITERATIONS_LONG);
	outb_p(0x81, SONYPI_CST_IOPORT);
	wait_on_command(0, inb_p(SONYPI_CST_IOPORT) & 2, ITERATIONS_LONG);
	outb_p(addr, SONYPI_DATA_IOPORT);
	wait_on_command(0, inb_p(SONYPI_CST_IOPORT) & 2, ITERATIONS_LONG);
	outb_p(value, SONYPI_DATA_IOPORT);
	wait_on_command(0, inb_p(SONYPI_CST_IOPORT) & 2, ITERATIONS_LONG);
	return 0;
}

static int sonypi_ec_read(u8 addr, u8 *value)
{
#ifdef CONFIG_ACPI_EC
	if (SONYPI_ACPI_ACTIVE)
		return ec_read(addr, value);
#endif
	wait_on_command(1, inb_p(SONYPI_CST_IOPORT) & 3, ITERATIONS_LONG);
	outb_p(0x80, SONYPI_CST_IOPORT);
	wait_on_command(0, inb_p(SONYPI_CST_IOPORT) & 2, ITERATIONS_LONG);
	outb_p(addr, SONYPI_DATA_IOPORT);
	wait_on_command(0, inb_p(SONYPI_CST_IOPORT) & 2, ITERATIONS_LONG);
	*value = inb_p(SONYPI_DATA_IOPORT);
	return 0;
}

static int ec_read16(u8 addr, u16 *value)
{
	u8 val_lb, val_hb;
	if (sonypi_ec_read(addr, &val_lb))
		return -1;
	if (sonypi_ec_read(addr + 1, &val_hb))
		return -1;
	*value = val_lb | (val_hb << 8);
	return 0;
}

/* Initializes the device - this comes from the AML code in the ACPI bios */
static void sonypi_type1_srs(void)
{
	u32 v;

	pci_read_config_dword(sonypi_device.dev, SONYPI_G10A, &v);
	v = (v & 0xFFFF0000) | ((u32) sonypi_device.ioport1);
	pci_write_config_dword(sonypi_device.dev, SONYPI_G10A, v);

	pci_read_config_dword(sonypi_device.dev, SONYPI_G10A, &v);
	v = (v & 0xFFF0FFFF) |
	    (((u32) sonypi_device.ioport1 ^ sonypi_device.ioport2) << 16);
	pci_write_config_dword(sonypi_device.dev, SONYPI_G10A, v);

	v = inl(SONYPI_IRQ_PORT);
	v &= ~(((u32) 0x3) << SONYPI_IRQ_SHIFT);
	v |= (((u32) sonypi_device.bits) << SONYPI_IRQ_SHIFT);
	outl(v, SONYPI_IRQ_PORT);

	pci_read_config_dword(sonypi_device.dev, SONYPI_G10A, &v);
	v = (v & 0xFF1FFFFF) | 0x00C00000;
	pci_write_config_dword(sonypi_device.dev, SONYPI_G10A, v);
}

static void sonypi_type2_srs(void)
{
	if (sonypi_ec_write(SONYPI_SHIB, (sonypi_device.ioport1 & 0xFF00) >> 8))
		printk(KERN_WARNING "ec_write failed\n");
	if (sonypi_ec_write(SONYPI_SLOB, sonypi_device.ioport1 & 0x00FF))
		printk(KERN_WARNING "ec_write failed\n");
	if (sonypi_ec_write(SONYPI_SIRQ, sonypi_device.bits))
		printk(KERN_WARNING "ec_write failed\n");
	udelay(10);
}

/* Disables the device - this comes from the AML code in the ACPI bios */
static void sonypi_type1_dis(void)
{
	u32 v;

	pci_read_config_dword(sonypi_device.dev, SONYPI_G10A, &v);
	v = v & 0xFF3FFFFF;
	pci_write_config_dword(sonypi_device.dev, SONYPI_G10A, v);

	v = inl(SONYPI_IRQ_PORT);
	v |= (0x3 << SONYPI_IRQ_SHIFT);
	outl(v, SONYPI_IRQ_PORT);
}

static void sonypi_type2_dis(void)
{
	if (sonypi_ec_write(SONYPI_SHIB, 0))
		printk(KERN_WARNING "ec_write failed\n");
	if (sonypi_ec_write(SONYPI_SLOB, 0))
		printk(KERN_WARNING "ec_write failed\n");
	if (sonypi_ec_write(SONYPI_SIRQ, 0))
		printk(KERN_WARNING "ec_write failed\n");
}

static u8 sonypi_call1(u8 dev)
{
	u8 v1, v2;

	wait_on_command(0, inb_p(sonypi_device.ioport2) & 2, ITERATIONS_LONG);
	outb(dev, sonypi_device.ioport2);
	v1 = inb_p(sonypi_device.ioport2);
	v2 = inb_p(sonypi_device.ioport1);
	return v2;
}

static u8 sonypi_call2(u8 dev, u8 fn)
{
	u8 v1;

	wait_on_command(0, inb_p(sonypi_device.ioport2) & 2, ITERATIONS_LONG);
	outb(dev, sonypi_device.ioport2);
	wait_on_command(0, inb_p(sonypi_device.ioport2) & 2, ITERATIONS_LONG);
	outb(fn, sonypi_device.ioport1);
	v1 = inb_p(sonypi_device.ioport1);
	return v1;
}

static u8 sonypi_call3(u8 dev, u8 fn, u8 v)
{
	u8 v1;

	wait_on_command(0, inb_p(sonypi_device.ioport2) & 2, ITERATIONS_LONG);
	outb(dev, sonypi_device.ioport2);
	wait_on_command(0, inb_p(sonypi_device.ioport2) & 2, ITERATIONS_LONG);
	outb(fn, sonypi_device.ioport1);
	wait_on_command(0, inb_p(sonypi_device.ioport2) & 2, ITERATIONS_LONG);
	outb(v, sonypi_device.ioport1);
	v1 = inb_p(sonypi_device.ioport1);
	return v1;
}

#if 0
/* Get brightness, hue etc. Unreliable... */
static u8 sonypi_read(u8 fn)
{
	u8 v1, v2;
	int n = 100;

	while (n--) {
		v1 = sonypi_call2(0x8f, fn);
		v2 = sonypi_call2(0x8f, fn);
		if (v1 == v2 && v1 != 0xff)
			return v1;
	}
	return 0xff;
}
#endif

/* Set brightness, hue etc */
static void sonypi_set(u8 fn, u8 v)
{
	wait_on_command(0, sonypi_call3(0x90, fn, v), ITERATIONS_SHORT);
}

/* Tests if the camera is ready */
static int sonypi_camera_ready(void)
{
	u8 v;

	v = sonypi_call2(0x8f, SONYPI_CAMERA_STATUS);
	return (v != 0xff && (v & SONYPI_CAMERA_STATUS_READY));
}

/* Turns the camera off */
static void sonypi_camera_off(void)
{
	sonypi_set(SONYPI_CAMERA_PICTURE, SONYPI_CAMERA_MUTE_MASK);

	if (!sonypi_device.camera_power)
		return;

	sonypi_call2(0x91, 0);
	sonypi_device.camera_power = 0;
}

/* Turns the camera on */
static void sonypi_camera_on(void)
{
	int i, j;

	if (sonypi_device.camera_power)
		return;

	for (j = 5; j > 0; j--) {

		while (sonypi_call2(0x91, 0x1)) {
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(1);
		}
		sonypi_call1(0x93);

		for (i = 400; i > 0; i--) {
			if (sonypi_camera_ready())
				break;
			set_current_state(TASK_UNINTERRUPTIBLE);
			schedule_timeout(1);
		}
		if (i)
			break;
	}

	if (j == 0) {
		printk(KERN_WARNING "sonypi: failed to power on camera\n");
		return;
	}

	sonypi_set(0x10, 0x5a);
	sonypi_device.camera_power = 1;
}

/* sets the bluetooth subsystem power state */
static void sonypi_setbluetoothpower(u8 state)
{
	state = !!state;

	if (sonypi_device.bluetooth_power == state)
		return;

	sonypi_call2(0x96, state);
	sonypi_call1(0x82);
	sonypi_device.bluetooth_power = state;
}

static void input_keyrelease(void *data)
{
	struct input_dev *input_dev;
	int key;

	while (1) {
		if (kfifo_get(sonypi_device.input_fifo,
			      (unsigned char *)&input_dev,
			      sizeof(input_dev)) != sizeof(input_dev))
			return;
		if (kfifo_get(sonypi_device.input_fifo,
			      (unsigned char *)&key,
			      sizeof(key)) != sizeof(key))
			return;

		msleep(10);
		input_report_key(input_dev, key, 0);
		input_sync(input_dev);
	}
}

/* Interrupt handler: some event is available */
static irqreturn_t sonypi_irq(int irq, void *dev_id, struct pt_regs *regs)
{
	u8 v1, v2, event = 0;
	int i, j;

	v1 = inb_p(sonypi_device.ioport1);
	v2 = inb_p(sonypi_device.ioport1 + sonypi_device.evtype_offset);

	for (i = 0; sonypi_eventtypes[i].model; i++) {
		if (sonypi_device.model != sonypi_eventtypes[i].model)
			continue;
		if ((v2 & sonypi_eventtypes[i].data) !=
		    sonypi_eventtypes[i].data)
			continue;
		if (!(mask & sonypi_eventtypes[i].mask))
			continue;
		for (j = 0; sonypi_eventtypes[i].events[j].event; j++) {
			if (v1 == sonypi_eventtypes[i].events[j].data) {
				event = sonypi_eventtypes[i].events[j].event;
				goto found;
			}
		}
	}

	if (verbose)
		printk(KERN_WARNING
		       "sonypi: unknown event port1=0x%02x,port2=0x%02x\n",
		       v1, v2);
	/* We need to return IRQ_HANDLED here because there *are*
	 * events belonging to the sonypi device we don't know about,
	 * but we still don't want those to pollute the logs... */
	return IRQ_HANDLED;

found:
	if (verbose > 1)
		printk(KERN_INFO
		       "sonypi: event port1=0x%02x,port2=0x%02x\n", v1, v2);

	if (useinput) {
		struct input_dev *input_jog_dev = &sonypi_device.input_jog_dev;
		struct input_dev *input_key_dev = &sonypi_device.input_key_dev;
		switch (event) {
		case SONYPI_EVENT_JOGDIAL_UP:
		case SONYPI_EVENT_JOGDIAL_UP_PRESSED:
			input_report_rel(input_jog_dev, REL_WHEEL, 1);
			break;
		case SONYPI_EVENT_JOGDIAL_DOWN:
		case SONYPI_EVENT_JOGDIAL_DOWN_PRESSED:
			input_report_rel(input_jog_dev, REL_WHEEL, -1);
			break;
		case SONYPI_EVENT_JOGDIAL_PRESSED: {
			int key = BTN_MIDDLE;
			input_report_key(input_jog_dev, key, 1);
			kfifo_put(sonypi_device.input_fifo,
				  (unsigned char *)&input_jog_dev,
				  sizeof(input_jog_dev));
			kfifo_put(sonypi_device.input_fifo,
				  (unsigned char *)&key, sizeof(key));
			break;
		}
		case SONYPI_EVENT_FNKEY_RELEASED:
			/* Nothing, not all VAIOs generate this event */
			break;
		}
		input_sync(input_jog_dev);

		for (i = 0; sonypi_inputkeys[i].sonypiev; i++) {
			int key;

			if (event != sonypi_inputkeys[i].sonypiev)
				continue;

			key = sonypi_inputkeys[i].inputev;
			input_report_key(input_key_dev, key, 1);
			kfifo_put(sonypi_device.input_fifo,
				  (unsigned char *)&input_key_dev,
				  sizeof(input_key_dev));
			kfifo_put(sonypi_device.input_fifo,
				  (unsigned char *)&key, sizeof(key));
		}
		input_sync(input_key_dev);
		schedule_work(&sonypi_device.input_work);
	}

	kfifo_put(sonypi_device.fifo, (unsigned char *)&event, sizeof(event));
	kill_fasync(&sonypi_device.fifo_async, SIGIO, POLL_IN);
	wake_up_interruptible(&sonypi_device.fifo_proc_list);

	return IRQ_HANDLED;
}

/* External camera command (exported to the motion eye v4l driver) */
int sonypi_camera_command(int command, u8 value)
{
	if (!camera)
		return -EIO;

	down(&sonypi_device.lock);

	switch (command) {
	case SONYPI_COMMAND_SETCAMERA:
		if (value)
			sonypi_camera_on();
		else
			sonypi_camera_off();
		break;
	case SONYPI_COMMAND_SETCAMERABRIGHTNESS:
		sonypi_set(SONYPI_CAMERA_BRIGHTNESS, value);
		break;
	case SONYPI_COMMAND_SETCAMERACONTRAST:
		sonypi_set(SONYPI_CAMERA_CONTRAST, value);
		break;
	case SONYPI_COMMAND_SETCAMERAHUE:
		sonypi_set(SONYPI_CAMERA_HUE, value);
		break;
	case SONYPI_COMMAND_SETCAMERACOLOR:
		sonypi_set(SONYPI_CAMERA_COLOR, value);
		break;
	case SONYPI_COMMAND_SETCAMERASHARPNESS:
		sonypi_set(SONYPI_CAMERA_SHARPNESS, value);
		break;
	case SONYPI_COMMAND_SETCAMERAPICTURE:
		sonypi_set(SONYPI_CAMERA_PICTURE, value);
		break;
	case SONYPI_COMMAND_SETCAMERAAGC:
		sonypi_set(SONYPI_CAMERA_AGC, value);
		break;
	default:
		printk(KERN_ERR "sonypi: sonypi_camera_command invalid: %d\n",
		       command);
		break;
	}
	up(&sonypi_device.lock);
	return 0;
}

EXPORT_SYMBOL(sonypi_camera_command);

static int sonypi_misc_fasync(int fd, struct file *filp, int on)
{
	int retval;

	retval = fasync_helper(fd, filp, on, &sonypi_device.fifo_async);
	if (retval < 0)
		return retval;
	return 0;
}

static int sonypi_misc_release(struct inode *inode, struct file *file)
{
	sonypi_misc_fasync(-1, file, 0);
	down(&sonypi_device.lock);
	sonypi_device.open_count--;
	up(&sonypi_device.lock);
	return 0;
}

static int sonypi_misc_open(struct inode *inode, struct file *file)
{
	down(&sonypi_device.lock);
	/* Flush input queue on first open */
	if (!sonypi_device.open_count)
		kfifo_reset(sonypi_device.fifo);
	sonypi_device.open_count++;
	up(&sonypi_device.lock);
	return 0;
}

static ssize_t sonypi_misc_read(struct file *file, char __user *buf,
				size_t count, loff_t *pos)
{
	ssize_t ret;
	unsigned char c;

	if ((kfifo_len(sonypi_device.fifo) == 0) &&
	    (file->f_flags & O_NONBLOCK))
		return -EAGAIN;

	ret = wait_event_interruptible(sonypi_device.fifo_proc_list,
				       kfifo_len(sonypi_device.fifo) != 0);
	if (ret)
		return ret;

	while (ret < count &&
	       (kfifo_get(sonypi_device.fifo, &c, sizeof(c)) == sizeof(c))) {
		if (put_user(c, buf++))
			return -EFAULT;
		ret++;
	}

	if (ret > 0)
		file->f_dentry->d_inode->i_atime = CURRENT_TIME;

	return ret;
}

static unsigned int sonypi_misc_poll(struct file *file, poll_table *wait)
{
	poll_wait(file, &sonypi_device.fifo_proc_list, wait);
	if (kfifo_len(sonypi_device.fifo))
		return POLLIN | POLLRDNORM;
	return 0;
}

static int sonypi_misc_ioctl(struct inode *ip, struct file *fp,
			     unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	void __user *argp = (void __user *)arg;
	u8 val8;
	u16 val16;

	down(&sonypi_device.lock);
	switch (cmd) {
	case SONYPI_IOCGBRT:
		if (sonypi_ec_read(SONYPI_LCD_LIGHT, &val8)) {
			ret = -EIO;
			break;
		}
		if (copy_to_user(argp, &val8, sizeof(val8)))
			ret = -EFAULT;
		break;
	case SONYPI_IOCSBRT:
		if (copy_from_user(&val8, argp, sizeof(val8))) {
			ret = -EFAULT;
			break;
		}
		if (sonypi_ec_write(SONYPI_LCD_LIGHT, val8))
			ret = -EIO;
		break;
	case SONYPI_IOCGBAT1CAP:
		if (ec_read16(SONYPI_BAT1_FULL, &val16)) {
			ret = -EIO;
			break;
		}
		if (copy_to_user(argp, &val16, sizeof(val16)))
			ret = -EFAULT;
		break;
	case SONYPI_IOCGBAT1REM:
		if (ec_read16(SONYPI_BAT1_LEFT, &val16)) {
			ret = -EIO;
			break;
		}
		if (copy_to_user(argp, &val16, sizeof(val16)))
			ret = -EFAULT;
		break;
	case SONYPI_IOCGBAT2CAP:
		if (ec_read16(SONYPI_BAT2_FULL, &val16)) {
			ret = -EIO;
			break;
		}
		if (copy_to_user(argp, &val16, sizeof(val16)))
			ret = -EFAULT;
		break;
	case SONYPI_IOCGBAT2REM:
		if (ec_read16(SONYPI_BAT2_LEFT, &val16)) {
			ret = -EIO;
			break;
		}
		if (copy_to_user(argp, &val16, sizeof(val16)))
			ret = -EFAULT;
		break;
	case SONYPI_IOCGBATFLAGS:
		if (sonypi_ec_read(SONYPI_BAT_FLAGS, &val8)) {
			ret = -EIO;
			break;
		}
		val8 &= 0x07;
		if (copy_to_user(argp, &val8, sizeof(val8)))
			ret = -EFAULT;
		break;
	case SONYPI_IOCGBLUE:
		val8 = sonypi_device.bluetooth_power;
		if (copy_to_user(argp, &val8, sizeof(val8)))
			ret = -EFAULT;
		break;
	case SONYPI_IOCSBLUE:
		if (copy_from_user(&val8, argp, sizeof(val8))) {
			ret = -EFAULT;
			break;
		}
		sonypi_setbluetoothpower(val8);
		break;
	default:
		ret = -EINVAL;
	}
	up(&sonypi_device.lock);
	return ret;
}

static struct file_operations sonypi_misc_fops = {
	.owner		= THIS_MODULE,
	.read		= sonypi_misc_read,
	.poll		= sonypi_misc_poll,
	.open		= sonypi_misc_open,
	.release	= sonypi_misc_release,
	.fasync		= sonypi_misc_fasync,
	.ioctl		= sonypi_misc_ioctl,
};

struct miscdevice sonypi_misc_device = {
	.minor		= -1,
	.name		= "sonypi",
	.fops		= &sonypi_misc_fops,
};

static void sonypi_enable(unsigned int camera_on)
{
	if (sonypi_device.model == SONYPI_DEVICE_MODEL_TYPE2)
		sonypi_type2_srs();
	else
		sonypi_type1_srs();

	sonypi_call1(0x82);
	sonypi_call2(0x81, 0xff);
	sonypi_call1(compat ? 0x92 : 0x82);

	/* Enable ACPI mode to get Fn key events */
	if (!SONYPI_ACPI_ACTIVE && fnkeyinit)
		outb(0xf0, 0xb2);

	if (camera && camera_on)
		sonypi_camera_on();
}

static int sonypi_disable(void)
{
	sonypi_call2(0x81, 0);	/* make sure we don't get any more events */
	if (camera)
		sonypi_camera_off();

	/* disable ACPI mode */
	if (!SONYPI_ACPI_ACTIVE && fnkeyinit)
		outb(0xf1, 0xb2);

	if (sonypi_device.model == SONYPI_DEVICE_MODEL_TYPE2)
		sonypi_type2_dis();
	else
		sonypi_type1_dis();
	return 0;
}

#ifdef CONFIG_PM
static int old_camera_power;

static int sonypi_suspend(struct device *dev, u32 state, u32 level)
{
	if (level == SUSPEND_DISABLE) {
		old_camera_power = sonypi_device.camera_power;
		sonypi_disable();
	}
	return 0;
}

static int sonypi_resume(struct device *dev, u32 level)
{
	if (level == RESUME_ENABLE)
		sonypi_enable(old_camera_power);
	return 0;
}
#endif

static void sonypi_shutdown(struct device *dev)
{
	sonypi_disable();
}

static struct device_driver sonypi_driver = {
	.name		= "sonypi",
	.bus		= &platform_bus_type,
#ifdef CONFIG_PM
	.suspend	= sonypi_suspend,
	.resume		= sonypi_resume,
#endif
	.shutdown	= sonypi_shutdown,
};

static int __devinit sonypi_probe(void)
{
	int i, ret;
	struct sonypi_ioport_list *ioport_list;
	struct sonypi_irq_list *irq_list;
	struct pci_dev *pcidev;

	pcidev = pci_get_device(PCI_VENDOR_ID_INTEL,
				PCI_DEVICE_ID_INTEL_82371AB_3, NULL);

	sonypi_device.dev = pcidev;
	sonypi_device.model = pcidev ?
		SONYPI_DEVICE_MODEL_TYPE1 : SONYPI_DEVICE_MODEL_TYPE2;

	sonypi_device.fifo_lock = SPIN_LOCK_UNLOCKED;
	sonypi_device.fifo = kfifo_alloc(SONYPI_BUF_SIZE, GFP_KERNEL,
					 &sonypi_device.fifo_lock);
	if (IS_ERR(sonypi_device.fifo)) {
		printk(KERN_ERR "sonypi: kfifo_alloc failed\n");
		ret = PTR_ERR(sonypi_device.fifo);
		goto out_fifo;
	}

	init_waitqueue_head(&sonypi_device.fifo_proc_list);
	init_MUTEX(&sonypi_device.lock);
	sonypi_device.bluetooth_power = -1;

	if (pcidev && pci_enable_device(pcidev)) {
		printk(KERN_ERR "sonypi: pci_enable_device failed\n");
		ret = -EIO;
		goto out_pcienable;
	}

	sonypi_misc_device.minor = (minor == -1) ? MISC_DYNAMIC_MINOR : minor;
	if ((ret = misc_register(&sonypi_misc_device))) {
		printk(KERN_ERR "sonypi: misc_register failed\n");
		goto out_miscreg;
	}

	if (sonypi_device.model == SONYPI_DEVICE_MODEL_TYPE2) {
		ioport_list = sonypi_type2_ioport_list;
		sonypi_device.region_size = SONYPI_TYPE2_REGION_SIZE;
		sonypi_device.evtype_offset = SONYPI_TYPE2_EVTYPE_OFFSET;
		irq_list = sonypi_type2_irq_list;
	} else {
		ioport_list = sonypi_type1_ioport_list;
		sonypi_device.region_size = SONYPI_TYPE1_REGION_SIZE;
		sonypi_device.evtype_offset = SONYPI_TYPE1_EVTYPE_OFFSET;
		irq_list = sonypi_type1_irq_list;
	}

	for (i = 0; ioport_list[i].port1; i++) {
		if (request_region(ioport_list[i].port1,
				   sonypi_device.region_size,
				   "Sony Programable I/O Device")) {
			/* get the ioport */
			sonypi_device.ioport1 = ioport_list[i].port1;
			sonypi_device.ioport2 = ioport_list[i].port2;
			break;
		}
	}
	if (!sonypi_device.ioport1) {
		printk(KERN_ERR "sonypi: request_region failed\n");
		ret = -ENODEV;
		goto out_reqreg;
	}

	for (i = 0; irq_list[i].irq; i++) {

		sonypi_device.irq = irq_list[i].irq;
		sonypi_device.bits = irq_list[i].bits;

		if (!request_irq(sonypi_device.irq, sonypi_irq,
				 SA_SHIRQ, "sonypi", sonypi_irq))
			break;
	}

	if (!irq_list[i].irq) {
		printk(KERN_ERR "sonypi: request_irq failed\n");
		ret = -ENODEV;
		goto out_reqirq;
	}

	if (useinput) {
		/* Initialize the Input Drivers: jogdial */
		int i;
		sonypi_device.input_jog_dev.evbit[0] =
			BIT(EV_KEY) | BIT(EV_REL);
		sonypi_device.input_jog_dev.keybit[LONG(BTN_MOUSE)] =
			BIT(BTN_MIDDLE);
		sonypi_device.input_jog_dev.relbit[0] = BIT(REL_WHEEL);
		sonypi_device.input_jog_dev.name =
			kmalloc(sizeof(SONYPI_JOG_INPUTNAME), GFP_KERNEL);
		if (!sonypi_device.input_jog_dev.name) {
			printk(KERN_ERR "sonypi: kmalloc failed\n");
			ret = -ENOMEM;
			goto out_inkmallocinput1;
		}
		sprintf(sonypi_device.input_jog_dev.name, SONYPI_JOG_INPUTNAME);
		sonypi_device.input_jog_dev.id.bustype = BUS_ISA;
		sonypi_device.input_jog_dev.id.vendor = PCI_VENDOR_ID_SONY;

		input_register_device(&sonypi_device.input_jog_dev);
		printk(KERN_INFO "%s input method installed.\n",
		       sonypi_device.input_jog_dev.name);

		/* Initialize the Input Drivers: special keys */
		sonypi_device.input_key_dev.evbit[0] = BIT(EV_KEY);
		for (i = 0; sonypi_inputkeys[i].sonypiev; i++)
			if (sonypi_inputkeys[i].inputev)
				set_bit(sonypi_inputkeys[i].inputev,
					sonypi_device.input_key_dev.keybit);
		sonypi_device.input_key_dev.name =
			kmalloc(sizeof(SONYPI_KEY_INPUTNAME), GFP_KERNEL);
		if (!sonypi_device.input_key_dev.name) {
			printk(KERN_ERR "sonypi: kmalloc failed\n");
			ret = -ENOMEM;
			goto out_inkmallocinput2;
		}
		sprintf(sonypi_device.input_key_dev.name, SONYPI_KEY_INPUTNAME);
		sonypi_device.input_key_dev.id.bustype = BUS_ISA;
		sonypi_device.input_key_dev.id.vendor = PCI_VENDOR_ID_SONY;

		input_register_device(&sonypi_device.input_key_dev);
		printk(KERN_INFO "%s input method installed.\n",
		       sonypi_device.input_key_dev.name);

		sonypi_device.input_fifo_lock = SPIN_LOCK_UNLOCKED;
		sonypi_device.input_fifo =
			kfifo_alloc(SONYPI_BUF_SIZE, GFP_KERNEL,
				    &sonypi_device.input_fifo_lock);
		if (IS_ERR(sonypi_device.input_fifo)) {
			printk(KERN_ERR "sonypi: kfifo_alloc failed\n");
			ret = PTR_ERR(sonypi_device.input_fifo);
			goto out_infifo;
		}

		INIT_WORK(&sonypi_device.input_work, input_keyrelease, NULL);
	}

	sonypi_device.pdev = platform_device_register_simple("sonypi", -1,
							     NULL, 0);
	if (IS_ERR(sonypi_device.pdev)) {
		ret = PTR_ERR(sonypi_device.pdev);
		goto out_platformdev;
	}

	sonypi_enable(0);

	printk(KERN_INFO "sonypi: Sony Programmable I/O Controller Driver"
	       "v%s.\n", SONYPI_DRIVER_VERSION);
	printk(KERN_INFO "sonypi: detected %s model, "
	       "verbose = %d, fnkeyinit = %s, camera = %s, "
	       "compat = %s, mask = 0x%08lx, useinput = %s, acpi = %s\n",
	       (sonypi_device.model == SONYPI_DEVICE_MODEL_TYPE1) ?
			"type1" : "type2",
	       verbose,
	       fnkeyinit ? "on" : "off",
	       camera ? "on" : "off",
	       compat ? "on" : "off",
	       mask,
	       useinput ? "on" : "off",
	       SONYPI_ACPI_ACTIVE ? "on" : "off");
	printk(KERN_INFO "sonypi: enabled at irq=%d, port1=0x%x, port2=0x%x\n",
	       sonypi_device.irq,
	       sonypi_device.ioport1, sonypi_device.ioport2);

	if (minor == -1)
		printk(KERN_INFO "sonypi: device allocated minor is %d\n",
		       sonypi_misc_device.minor);

	return 0;

out_platformdev:
	kfifo_free(sonypi_device.input_fifo);
out_infifo:
	input_unregister_device(&sonypi_device.input_key_dev);
	kfree(sonypi_device.input_key_dev.name);
out_inkmallocinput2:
	input_unregister_device(&sonypi_device.input_jog_dev);
	kfree(sonypi_device.input_jog_dev.name);
out_inkmallocinput1:
	free_irq(sonypi_device.irq, sonypi_irq);
out_reqirq:
	release_region(sonypi_device.ioport1, sonypi_device.region_size);
out_reqreg:
	misc_deregister(&sonypi_misc_device);
out_miscreg:
	if (pcidev)
		pci_disable_device(pcidev);
out_pcienable:
	kfifo_free(sonypi_device.fifo);
out_fifo:
	pci_dev_put(sonypi_device.dev);
	return ret;
}

static void __devexit sonypi_remove(void)
{
	sonypi_disable();

	platform_device_unregister(sonypi_device.pdev);

	if (useinput) {
		input_unregister_device(&sonypi_device.input_key_dev);
		kfree(sonypi_device.input_key_dev.name);
		input_unregister_device(&sonypi_device.input_jog_dev);
		kfree(sonypi_device.input_jog_dev.name);
		kfifo_free(sonypi_device.input_fifo);
	}

	free_irq(sonypi_device.irq, sonypi_irq);
	release_region(sonypi_device.ioport1, sonypi_device.region_size);
	misc_deregister(&sonypi_misc_device);
	if (sonypi_device.dev)
		pci_disable_device(sonypi_device.dev);
	kfifo_free(sonypi_device.fifo);
	pci_dev_put(sonypi_device.dev);
	printk(KERN_INFO "sonypi: removed.\n");
}

static struct dmi_system_id __initdata sonypi_dmi_table[] = {
	{
		.ident = "Sony Vaio",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Sony Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "PCG-"),
		},
	},
	{
		.ident = "Sony Vaio",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Sony Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "VGN-"),
		},
	},
	{ }
};

static int __init sonypi_init(void)
{
	int ret;

	if (!dmi_check_system(sonypi_dmi_table))
		return -ENODEV;

	ret = driver_register(&sonypi_driver);
	if (ret)
		return ret;

	ret = sonypi_probe();
	if (ret)
		driver_unregister(&sonypi_driver);

	return ret;
}

static void __exit sonypi_exit(void)
{
	driver_unregister(&sonypi_driver);
	sonypi_remove();
}

module_init(sonypi_init);
module_exit(sonypi_exit);
