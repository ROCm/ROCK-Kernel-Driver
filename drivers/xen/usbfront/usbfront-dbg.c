/*
 * usbfront-dbg.c
 *
 * Xen USB Virtual Host Controller - debugging
 *
 * Copyright (C) 2009, FUJITSU LABORATORIES LTD.
 * Author: Noboru Iwamatsu <n_iwamatsu@jp.fujitsu.com>
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * or, by your choice,
 *
 * When distributed separately from the Linux kernel or incorporated into
 * other software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

static ssize_t show_statistics(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct usb_hcd *hcd;
	struct usbfront_info *info;
	unsigned long flags;
	unsigned temp, size;
	char *next;

	hcd = dev_get_drvdata(dev);
	info = hcd_to_info(hcd);
	next = buf;
	size = PAGE_SIZE;

	spin_lock_irqsave(&info->lock, flags);

	temp = scnprintf(next, size,
			"bus %s, device %s\n"
			"%s\n"
			"xenhcd, hcd state %d\n",
			hcd->self.controller->bus->name,
			dev_name(hcd->self.controller),
			hcd->product_desc,
			hcd->state);
	size -= temp;
	next += temp;

#ifdef XENHCD_STATS
	temp = scnprintf(next, size,
		"complete %ld unlink %ld ring_full %ld\n",
		info->stats.complete, info->stats.unlink,
		info->stats.ring_full);
	size -= temp;
	next += temp;
#endif

	spin_unlock_irqrestore(&info->lock, flags);

	return PAGE_SIZE - size;
}

static DEVICE_ATTR(statistics, S_IRUGO, show_statistics, NULL);

static inline void create_debug_file(struct usbfront_info *info)
{
	struct device *dev = info_to_hcd(info)->self.controller;
	if (device_create_file(dev, &dev_attr_statistics))
		printk(KERN_WARNING "statistics file not created for %s\n",
		       info_to_hcd(info)->self.bus_name);
}

static inline void remove_debug_file(struct usbfront_info *info)
{
	struct device *dev = info_to_hcd(info)->self.controller;
	device_remove_file(dev, &dev_attr_statistics);
}
