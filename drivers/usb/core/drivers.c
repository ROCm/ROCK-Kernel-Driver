/*
 * drivers.c
 * (C) Copyright 1999 Randy Dunlap.
 * (C) Copyright 1999, 2000 Thomas Sailer <sailer@ife.ee.ethz.ch>. (proc file per device)
 * (C) Copyright 1999 Deti Fliegl (new USB architecture)
 *
 * $id$
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *************************************************************
 *
 * 1999-12-16: Thomas Sailer <sailer@ife.ee.ethz.ch>
 *   Converted the whole proc stuff to real
 *   read methods. Now not the whole device list needs to fit
 *   into one page, only the device list for one bus.
 *   Added a poll method to /proc/bus/usb/devices, to wake
 *   up an eventual usbd
 * 2000-01-04: Thomas Sailer <sailer@ife.ee.ethz.ch>
 *   Turned into its own filesystem
 *
 * $Id: drivers.c,v 1.3 2000/01/11 13:58:24 tom Exp $
 */

#include <linux/fs.h>
#include <linux/gfp.h>
#include <linux/usb.h>
#include <linux/usbdevice_fs.h>
#include <linux/smp_lock.h>
#include <asm/uaccess.h>

/*****************************************************************/

/*
 * Dump usb_driver_list.
 *
 * We now walk the list of registered USB drivers.
 */
static ssize_t usb_driver_read(struct file *file, char *buf, size_t nbytes, loff_t *ppos)
{
	struct list_head *tmp = usb_driver_list.next;
	char *page, *start, *end;
	ssize_t ret = 0;
	unsigned int pos, len;

	if (*ppos < 0)
		return -EINVAL;
	if (nbytes <= 0)
		return 0;
	if (!access_ok(VERIFY_WRITE, buf, nbytes))
		return -EFAULT;
        if (!(page = (char*) __get_free_page(GFP_KERNEL)))
                return -ENOMEM;
	start = page;
	end = page + (PAGE_SIZE - 100);
	pos = *ppos;
	lock_kernel(); /* else drivers might be unloaded */
	for (; tmp != &usb_driver_list; tmp = tmp->next) {
		struct usb_driver *driver = list_entry(tmp, struct usb_driver, driver_list);
		int minor = driver->fops ? driver->minor : -1;
		if (minor == -1)
			start += sprintf (start, "         %s\n", driver->name);
		else if (driver->num_minors == 1)
			start += sprintf (start, "    %3d: %s\n", minor, driver->name);
		else
			start += sprintf (start, "%3d-%3d: %s\n", minor, minor + driver->num_minors - 1, driver->name);
		if (start > end) {
			start += sprintf(start, "(truncated)\n");
			break;
		}
	}
	unlock_kernel();
	if (start == page)
		start += sprintf(start, "(none)\n");
	len = start - page;
	if (len > pos) {
		len -= pos;
		if (len > nbytes)
			len = nbytes;
		ret = len;
		if (copy_to_user(buf, page + pos, len))
			ret = -EFAULT;
		else
			*ppos += len;
	}
	free_page((unsigned long)page);
	return ret;
}

static loff_t usb_driver_lseek(struct file * file, loff_t offset, int orig)
{
	loff_t ret;

	lock_kernel();
	switch (orig) {
	case 0:
		file->f_pos = offset;
		ret = file->f_pos;
		break;
	case 1:
		file->f_pos += offset;
		ret = file->f_pos;
		break;
	case 2:
	default:
		ret = -EINVAL;
	}
	unlock_kernel();
	return ret;
}

struct file_operations usbdevfs_drivers_fops = {
	.llseek =	usb_driver_lseek,
	.read =		usb_driver_read,
};
